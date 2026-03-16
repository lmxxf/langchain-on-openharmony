# P7885 部署调试记录

## 设备信息

| 项目 | 值 |
|------|-----|
| 芯片 | P7885 (Cortex-A55, ARMv8.2-A) |
| 内核 | Linux 5.10.184 aarch64 |
| musl | 1.2.5 |
| 动态链接器 | `/system/lib/ld-musl-aarch64.so.1` |
| 存储 | 231G（228G 可用） |

## 核心问题

部署包从 RK3568（Linux 6.6）迁移到 P7885（Linux 5.10）时，`import ssl` / `import hashlib` 直接 Signal 11 (SIGSEGV)。

**根因：6 个 `.so` 模块没有显式链接 `libpython3.11.so`。**

| 模块 | 说明 |
|------|------|
| `_sha1.so` | SHA-1 哈希 |
| `_sha256.so` | SHA-256 哈希 |
| `_sha3.so` | SHA-3 哈希 |
| `_sha512.so` | SHA-512 哈希 |
| `_hashlib.so` | hashlib 后端（依赖 libcrypto） |
| `_ssl.so` | SSL/TLS（依赖 libssl + libcrypto） |

这些模块是通过 CPython Makefile 的 `Setup.stdlib` 编译的（不经过 `setup.py`），之前给 `BLDSHARED` 加 `-lpython3.11` 的 fix 没覆盖到它们。

RK3568 上能跑是因为那台设备的 musl 版本在 `dlopen` 时会从调用进程继承符号。P7885 的 musl 1.2.5 更严格，不继承——每个 `.so` 必须显式声明自己的依赖。

**修复：`patchelf --add-needed libpython3.11.so` 给 6 个 `.so` 补上依赖。**

## 另一个坑：系统时间

刷机后时间重置到 2008-01-01，SSL 证书验证报 `certificate is not yet valid`。

修复：`hdc shell 'date MMDDHHmmYYYY.SS'`

## 调试脚本说明

按排查顺序排列：

| 脚本 | 用途 | 结果 |
|------|------|------|
| `test_minimal.py` | 最小验证：`print("hello")` | ✅ 确认 Python 二进制能跑 |
| `test2.py` | 逐步 import sys/json/ssl，用 `print()` | ❌ Signal 11，stdout 没 flush 看不到断点 |
| `test3.py` | 单独 `import sys` | ✅ |
| `test4.py` | 单独 `import json` | ✅ |
| `test5.py` | 单独 `import ssl` | ❌ Signal 11 —— **锁定 ssl** |
| `test6.py` | 单独 `import _ssl` | ❌ Signal 11 |
| `test7.py` | 用 ctypes 手动 dlopen libcrypto | ❌ ctypes 自己就挂（缺 ffi_closure_free） |
| `test8.py` | 用 importlib.util.find_spec 查 _ssl | ❌ Signal 11 |
| `test9.py` | 批量 import 所有模块，用 `print()` | ❌ Signal 11，看不到断点（stdout 缓冲） |
| `test10.py` | 用 `sys.stdout.flush()` 逐步 import | 未使用（改用 os.write） |
| `test11.py` | 用 `os.write(1, ...)` 逐步 import _struct/math | ✅ 确认基础 .so 没问题 |
| `test12.py` | `os.write` 批量 import 23 个模块 + `_sha*` | ❌ 前 23 个 OK，`_sha256` Signal 11 |
| `test13.py` | 屏蔽 `_hashlib/_ssl`，测 pydantic | ✅ pydantic_core + pydantic OK |
| `test14.py` | 尝试 ctypes.util | ❌ ctypes 不可用 |
| `test15.py` | 删掉 `_hashlib/_ssl` 后测 hashlib fallback | ❌ hashlib 内部还是触发了 `_sha256` |
| `test16.py` | **关键脚本**：逐个测全部 31 个 .so 模块 | ❌ 定位到 `_sha256` 是第一个 crash 的 |
| `test_deploy.py` | 综合部署检查（sys/json/ssl/pydantic/langchain） | ❌ 修复前 crash |
| `test_imports.py` | 分组 import（stdlib / Rust 扩展 / 纯 Python） | ❌ 修复前 crash |
| `test_full.py` | 完整 import 验证（stdlib + pydantic + langchain） | ✅ **patchelf 修复后全通** |
| `test_llm_full.py` | 端到端验证：LangChain + DeepSeek API 调用 | ✅ **全链路打通** |

## 调试方法论

1. **二分法定位**：先 `print("hello")`（最小集），再逐步加 import
2. **stdout 缓冲陷阱**：`print()` 在 crash 前可能没 flush，用 `os.write(1, b"...")` 直接写 fd
3. **逐模块扫描**：`test16.py` 一次性测 31 个 `.so`，精确定位到 `_sha256`
4. **`readelf -d` 查依赖**：发现 6 个 `.so` 缺 `libpython3.11.so` NEEDED 项
5. **`patchelf --add-needed`**：不用重编，直接给 ELF 补依赖

## 修复命令

```bash
# 安装 patchelf
sudo apt-get install patchelf

# 给 6 个 .so 补上 libpython 依赖
for f in _hashlib _sha1 _sha256 _sha3 _sha512 _ssl; do
    patchelf --add-needed libpython3.11.so \
        "lib-dynload/${f}.cpython-311-aarch64-linux-ohos.so"
done

# 验证
readelf -d lib-dynload/_sha256.cpython-311-aarch64-linux-ohos.so | grep libpython
# 应该看到: Shared library: [libpython3.11.so]
```

## 一键部署脚本

以上所有步骤（patchelf + 打包 + 推送 + 解压 + 校时 + 验证）已自动化为 `deploy-langchain.sh`：

```bash
# WSL 里跑，hdc 连上设备即可
cd /home/lmxxf/work/langchain-on-openharmony
./deploy-langchain.sh
```

RK3568 和 P7885 通用。patchelf 是幂等的——已 patch 的跳过，没 patch 的补上。

## 运行命令

```bash
hdc shell 'LD_LIBRARY_PATH=/data/local/tmp/lib \
  PYTHONHOME=/data/local/tmp/python-home \
  SSL_CERT_FILE=/data/local/tmp/cacert.pem \
  /data/local/tmp/python-home/bin/python3.11 your_script.py'
```

## 结论

**OHOS musl dlopen 不继承进程符号**——这是第三次碰到同一个根因了：

1. CPython 自己的 `.so` 模块（`BLDSHARED` 加 `-lpython3.11`）
2. Rust 扩展 pydantic-core（`RUSTFLAGS="-l python3.11"`）
3. **本次：CPython 的 hash/ssl 模块**（`patchelf --add-needed`）

以后往 OHOS 移植任何 Python 扩展，**第一件事就是 `readelf -d xxx.so | grep libpython`，没有就补。**
