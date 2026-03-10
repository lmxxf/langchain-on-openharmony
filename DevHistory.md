# LangChain on OpenHarmony 6.0 — 开发记录

## 2026-03-10 第一次讨论：可行性分析

### 目标

把使用了 LangChain 的 Python 程序跑在 OpenHarmony 6.0（RK3568，aarch64）上。

### 核心问题

LangChain 是 Python 生态的产物，OH 上没有现成的 Python 运行时。

### 技术栈关系

```
Linux 内核（syscall）
    ↑
┌───────────┬───────────┐
│  glibc    │   musl    │  ← C标准库，二选一，二进制不兼容
│ (Ubuntu)  │ (OH/Alpine)│
└───────────┴───────────┘
    ↑
CPython（C写的Python解释器，链接上面某一个libc）
    ↑
LangChain（纯Python包，跑在CPython上面）
    ↑
Nuitka（可选：把Python代码编译成C，但还是依赖CPython运行时）
```

**关键概念：**
- **glibc vs musl**：都是 C 标准库（程序和 Linux 内核之间的翻译层），Ubuntu 用 glibc，OH 用 musl，**二进制不兼容**——glibc 编译的程序直接扔 OH 上跑不了
- **CPython**：Python 语言的官方实现，用 C 写的解释器（~50万行C代码）。`python3` 命令就是它编译出来的二进制
- **Nuitka**：Python→C 编译器，但生成的 C 代码仍依赖 CPython 运行时（libpython），不是真正脱离 Python

### OH 工具链现状（已确认）

| 组件 | 路径 | 状态 |
|------|------|------|
| 交叉编译器 | `prebuilts/ohos-sdk/linux/20/native/llvm/bin/aarch64-unknown-linux-ohos-clang` | ✅ Clang 15 |
| musl sysroot | `prebuilts/ohos-sdk/linux/20/native/sysroot/usr/lib/aarch64-linux-ohos/` | ✅ libc.a/so, crt*.o 全套 |
| musl libc（编译产物） | `out/sdk/obj/third_party/musl/usr/lib/aarch64-linux-ohos/` | ✅ libc.a, libc.so, libpthread.a 等 |
| C++ 运行时 | `prebuilts/clang/ohos/linux-x86_64/llvm/lib/aarch64-linux-ohos/libc++.a` | ✅ |
| 宿主机 Python | `prebuilts/python/linux-x86/3.11.4/` | ✅ x86 版，编译时用，不是目标平台的 |

**OH target triple**: `aarch64-unknown-linux-ohos`（不是标准 linux-gnu 也不是 linux-musl）

### 移植方案评估

| 方案 | 可行性 | 说明 |
|------|--------|------|
| 直接搬 Ubuntu 上的 python3 二进制 | ❌ | 链接 glibc，OH 上没有 |
| 用 OH Clang 交叉编译 CPython 源码 | ✅ 首选 | 静态链接 musl 可绕过动态链接器差异 |
| Nuitka 编译成二进制 | ⚠️ | 最终还是要 CPython 运行时，等于先走上一步 |
| Python 后端 + OH 前端调 HTTP | ✅ 备选 | LangChain 跑服务器，OH 只做 UI |
| 直接移植 LangChain.js 到 ArkTS | ❌ | 依赖链太深，npm 生态不可用 |

### 交叉编译 CPython 的关键点

1. 用 `aarch64-unknown-linux-ohos-clang` 编译 CPython 3.11 源码
2. `--with-sysroot` 指向 OH 的 musl sysroot
3. **静态链接 musl**（关键！绕过 OH 的动态链接器路径 `/system/lib64/ld-musl-aarch64.so.1` 和命名空间隔离 `ld-musl-namespace`）
4. 得到能在 OH 上裸跑的 `python3` 二进制
5. LangChain 是纯 Python 包，CPython 跑通后 pip install 即可

### OH 的 musl 特殊性

- 动态链接器路径：`/system/lib64/ld-musl-aarch64.so.1`（不是标准的 `/lib/`）
- 有 `ld-musl-namespace` 机制，动态库加载受限
- 内核有定制 syscall（安全沙箱相关）
- **静态链接可绕过以上所有问题**

### Python 库的真正难点

CPython 本体能编过（已有人实践成功）。**真正的坑是 Python 第三方库：**

**纯 Python 库 → 没问题**，.py 文件直接丢进去就跑，不挑 libc 不挑架构。LangChain 核心大部分是纯 Python。

**C/Rust 扩展库 → 每个都要交叉编译**，这才是工程量：

| 库 | 底层语言 | 作用 | LangChain 必须？ |
|---|---|---|---|
| **pydantic-core** | Rust | 数据校验，LangChain 骨架 | ✅ 必须 |
| **cryptography** | Rust + C (OpenSSL) | HTTPS/TLS | ✅ 必须（网络调用） |
| **aiohttp** (multidict/yarl/frozenlist) | C | 异步 HTTP | ⚠️ 可用 httpx 替代 |
| **numpy** | C/Fortran | 数值计算 | ⚠️ 看用不用向量 |
| **orjson** | Rust | 快速 JSON | ❌ 可 fallback 标准库 json |
| **tiktoken** | Rust | token 计数 | ⚠️ 看用不用 OpenAI |
| **SQLAlchemy** (greenlet) | C | 数据库 | ⚠️ 看需求 |

**最头疼的两个：pydantic-core（Rust）和 cryptography（Rust+OpenSSL）**——必须用 OH toolchain 交叉编译 Rust 到 `aarch64-unknown-linux-ohos`。

**依赖链地狱：**
```
pip install langchain
    ↓ 拉几十个依赖
    ↓ 纯 Python 的 → ✅ 直接用
    ↓ 有 C/Rust 扩展的 → 💀 每个都要交叉编译
    ↓ 依赖 OpenSSL 的 → 💀💀 还得先编译 OpenSSL for musl-ohos
```

### 最小验证路径

先跑最小集，别一上来全家桶：

1. ~~CPython 能跑~~（已有人实践成功，跳过）
2. `import json, urllib.request` —— 纯标准库，零依赖
3. `import ssl` —— 验证 OpenSSL 有没有
4. `pip install pydantic` —— 验证 Rust 交叉编译链
5. 以上都通了，`pip install langchain-core` 基本就能过

### 下一步

- [x] CPython for OH（已有人实践成功）
- [ ] 确认 OH 上的 python3 有没有 ssl 模块（即 OpenSSL 编过没有）
- [ ] 交叉编译 pydantic-core（Rust → aarch64-unknown-linux-ohos）
- [ ] 交叉编译 cryptography（Rust + OpenSSL → aarch64-unknown-linux-ohos）
- [ ] pip install langchain-core 最小集验证
- [ ] 跑 LangChain hello world（调一次 LLM API）

### RK3568 板子实测（2026-03-10）

```
hdc shell 操作方式：powershell.exe -Command "hdc shell '...'"
```

| 项目 | 结果 |
|------|------|
| 内核 | Linux 6.6.101 aarch64，Clang 15 编译 |
| 动态链接器 | `/system/lib/ld-musl-aarch64.so.1`（注意是 lib 不是 lib64） |
| Python | ❌ 没有 |
| OpenSSL (libssl/libcrypto) | ❌ 没有（只有 `libcrypto_framework_ani.z.so`，是 OH 自己的加密框架，不是 OpenSSL） |
| /data 分区 | f2fs，20G 总量，14G 可用 |
| toybox | ✅ 有（ls 等基础命令） |

**关键发现：**
- 动态链接器在 `/system/lib/` 不是 `/system/lib64/`（之前假设错了）
- **板子上没有 OpenSSL**——意味着 CPython 的 ssl 模块要么静态链接 OpenSSL，要么先编译 OpenSSL 推上去
- 空间充足（14G），不是瓶颈

**编译清单更新——需要交叉编译的东西：**
1. OpenSSL → `libssl.a` + `libcrypto.a`（静态）
2. CPython 3.11 → 带 ssl 模块，静态链接 musl + OpenSSL
3. pydantic-core（Rust）
4. cryptography（Rust + 上面编好的 OpenSSL）
5. LangChain 纯 Python 包直接丢进去

---

## 2026-03-10 实战：交叉编译全过程

### 编译产物

| 组件 | 版本 | 产物路径 | 大小 |
|------|------|----------|------|
| OpenSSL | 3.3.2 | `LangChain/build/openssl/lib/libssl.a + libcrypto.a` | 1.7MB + 8.7MB |
| zlib | 1.3.1 | `LangChain/build/zlib/lib/libz.a` | 120KB |
| libffi | 3.4.6 | `LangChain/build/libffi/lib/libffi.a` | 108KB |
| CPython | 3.11.11 | strip 后 11MB 静态二进制 | 11MB |

### 编译器 Wrapper（关键技巧）

CPython 的 configure 不支持 CC 带参数，用 wrapper 脚本绕过：

```bash
# /tmp/ohos-cc.sh
#!/bin/bash
exec /home/lmxxf/oh6/source/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang \
  --target=aarch64-unknown-linux-ohos \
  --sysroot=/home/lmxxf/oh6/source/prebuilts/ohos-sdk/linux/20/native/sysroot \
  "$@"
```

### 踩坑记录

| 坑 | 原因 | 解法 |
|---|---|---|
| `config.sub` 不认 `ohos` | CPython 的 autoconf 不知道 OHOS | `--host=aarch64-unknown-linux-musl` 骗它 |
| wrapper 脚本 `bad interpreter: ^M` | Write 工具写出 Windows 换行 | `sed -i 's/\r$//'` |
| `platform triplet` 冲突 | configure 检测出 `linux-gnu`，编译器报 `linux-ohos` | patch configure 跳过检查 |
| `preadv2/pwritev2` 未声明 | OH musl 没实现这俩函数 | pyconfig.h `#undef HAVE_PREADV2/PWRITEV2` |
| `gettext` 系列函数缺失 | OH musl 没有 libintl | pyconfig.h `#undef HAVE_LIBINTL_H` |
| `CAN_RAW_FD_FRAMES` 未定义 | OH 内核头文件没有 SocketCAN | pyconfig.h `#undef HAVE_LINUX_CAN_RAW_*` |
| `_ssl` 未检测到 | `--with-openssl` 路径被 shell 变量截断 | 全部硬编码绝对路径 |
| zlib 模块 "Failed to build" | Makefile 里 `-lz` 找不到 libz.a | `Setup.local` 强制指定完整路径 |
| `_blake2` 编译失败 | OH musl 没有 `explicit_bzero` | 放弃（hashlib 有 sha256 fallback） |
| `Dynamic loading not supported` | 静态链接的 python 不能 dlopen .so | **所有模块都编为内建（Setup.local）** |
| 标准库缺失 `No module named 'encodings'` | 只推了二进制，没推 .py 文件 | `PYTHONHOME` 指向标准库目录 |

### 关键设计决策：全部内建

静态链接的 python 无法加载 .so 动态库（`dlopen` 不可用）。因此所有关键模块必须通过 `Modules/Setup.local` 编为内建模块（builtin），直接链接进 python 二进制。

**Setup.local 中的 31 个内建模块：**
```
zlib _ssl _hashlib _socket binascii select _struct _json
_datetime math _random _sha256 _sha512 _sha1 _sha3 _md5
_pickle _asyncio _bisect _heapq _csv _posixsubprocess
_contextvars _typing _queue array fcntl mmap _opcode
_statistics unicodedata
```

### 板子上验证通过 ✅

```
$ PYTHONHOME=/data/local/tmp/python-home /data/local/tmp/python3 test_oh.py
Python 3.11.11 (main, Mar 10 2026, 20:22:12) [Clang 15.0.4]
zlib OK
ssl: OpenSSL 3.3.2 3 Sep 2024
socket OK
json OK
```

### 部署结构

```
/data/local/tmp/
├── python3                          # 11MB 静态二进制
└── python-home/
    └── lib/python3.11/              # 65MB 标准库（精简后）
        ├── encodings/
        ├── email/
        ├── http/
        ├── urllib/
        ├── json/
        ├── ssl.py
        └── ...（不需要 lib-dynload/）
```

### HTTPS + LLM API 验证通过 ✅

**SSL 证书**：板子上没有 CA 证书包，需要下载 Mozilla CA bundle：
```bash
wget https://curl.se/ca/cacert.pem
hdc file send cacert.pem /data/local/tmp/cacert.pem
# 运行时指定：SSL_CERT_FILE=/data/local/tmp/cacert.pem
```

**blake2 warning**：`_blake2` 模块未编入（OH musl 缺 `explicit_bzero`），hashlib 启动时会报 warning，不影响功能，sha256 等正常可用。

**HTTPS 请求**：
```
$ SSL_CERT_FILE=/data/local/tmp/cacert.pem PYTHONHOME=... python3 test_https.py
HTTPS OK!
Origin: 114.254.2.78
```

**DeepSeek API 调用**：
```
$ SSL_CERT_FILE=/data/local/tmp/cacert.pem PYTHONHOME=... python3 test_llm.py
LLM response: Hello, friend. How are you?
```

**全链路打通：OH RK3568 → Python 3.11 → HTTPS/TLS → DeepSeek API → JSON 响应**

### 下一步

- [x] OpenSSL 3.3.2 交叉编译
- [x] zlib 1.3.1 交叉编译
- [x] libffi 3.4.6 交叉编译
- [x] CPython 3.11.11 交叉编译（31 个内建模块）
- [x] 板子上 `import ssl, socket, json, zlib` 全部通过
- [x] HTTPS 请求通过（需 CA 证书包）
- [x] DeepSeek LLM API 调用成功
- [ ] 交叉编译 pydantic-core（Rust → aarch64-unknown-linux-ohos）
- [ ] 交叉编译 cryptography（Rust + OpenSSL）
- [ ] pip install langchain-core 最小集验证
- [ ] 跑 LangChain hello world（通过 LangChain 框架调 LLM）

### 编译脚本路径

- 宿主机 Python（bootstrap）：`/tmp/python-host/bin/python3.11`
- CPython 源码：`/tmp/Python-3.11.11/`
- CPython build 目录：`/tmp/Python-3.11.11/build-ohos/`
- OpenSSL 源码：`/tmp/openssl-3.3.2/`
- zlib 源码：`/tmp/zlib-1.3.1/`
- libffi 源码：`/tmp/libffi-3.4.6/`

---

## 新 Session 恢复指南

**下次开 session 时让朱雀读这个文件，就能接上。**

### 最终目标

**把最新版本的 LangChain 在 RK3568 的 OpenHarmony 6.0 上跑通。**

### 当前状态

Python 3.11.11 静态二进制已在 RK3568 上跑通，能调 DeepSeek API。下一步是装 LangChain。

### 板子上的文件

```
/data/local/tmp/
├── python3              # 11MB 静态二进制，已 chmod +x
├── cacert.pem           # Mozilla CA 证书包
├── python-home/
│   └── lib/python3.11/  # 65MB 精简版标准库
├── test_oh.py           # import ssl/socket/json/zlib 测试
├── test_https.py        # HTTPS 请求测试
└── test_llm.py          # DeepSeek API 调用测试
```

### 运行命令模板

```bash
# hdc 通过 powershell 调用
powershell.exe -Command "hdc shell '...'"

# 运行 Python（必须设这两个环境变量）
PYTHONHOME=/data/local/tmp/python-home SSL_CERT_FILE=/data/local/tmp/cacert.pem /data/local/tmp/python3 xxx.py
```

### 持久化文件（不怕丢）

```
/home/lmxxf/work/openclaw_on_openharmony/LangChain/
├── build/python3-ohos         # 最终二进制
├── build/Setup.local          # 31 个内建模块配置
├── build/configure.patched    # patch 过的 CPython configure
├── build/pyconfig.h.patched   # patch 过的 pyconfig.h
├── build/ohos-cc.sh           # 编译器 wrapper
├── build/ohos-cxx.sh          # C++ 编译器 wrapper
├── build/python-host.tar.gz   # 宿主机 Python bootstrap（101MB）
├── build/openssl/             # libssl.a + libcrypto.a
├── build/zlib/                # libz.a
├── build/libffi/              # libffi.a
├── build/python/              # 标准库（make install 产物）
└── sources/                   # 源码 tarball（从官网下的，未修改）
```

### /tmp 里的临时文件（WSL 重启会丢，可从上面重建）

重建步骤见 README.md。核心是解压源码 → 用 patched configure → 用 patched pyconfig.h → 用 Setup.local → make。

### 下一步技术要点（pydantic-core）

- pydantic-core 是 Rust 写的，需要 Rust 交叉编译到 `aarch64-unknown-linux-ohos`
- 需要：rustup target add（可能需要自定义 target spec，因为 ohos 不是 Rust 标准 target）
- 编出来的 .so 无法被静态 python 加载（dlopen 不可用）
- **可能的思路**：用纯 Python 版的 pydantic（v1 是纯 Python，v2 依赖 pydantic-core）
- **或者**：把 pydantic-core 的 Rust 代码编成 .a 链接进 python？（非常规，需要研究）
- **或者**：放弃静态链接，改用动态链接的 python（能 dlopen，但需要处理 ld-musl-namespace）

这是下一关的核心难题，需要先调研再动手。

---

*OH 工程路径: `/home/lmxxf/oh6/source`*
*GitHub: https://github.com/lmxxf/langchain-on-openharmony*
*项目路径: `/home/lmxxf/work/openclaw_on_openharmony/LangChain/`*
