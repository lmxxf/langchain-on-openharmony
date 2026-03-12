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

### 下一步

- [ ] 交叉编译 pydantic-core（Rust → aarch64-unknown-linux-ohos，Rust 官方 Tier 2 target）
- [ ] 交叉编译 cryptography（Rust + OpenSSL）
- [ ] pip install langchain-core 最小集验证
- [ ] 跑 LangChain hello world（通过 LangChain 框架调 LLM）

---

## 2026-03-11 上午 动态链接 CPython 重编

### 背景

静态链接的 CPython 不能 dlopen .so 文件，无法加载 pydantic-core（Rust 扩展）。LangChain 最新版（v0.3+）强制依赖 pydantic v2，pydantic v2 强制依赖 pydantic-core（Rust .so），没有纯 Python fallback。

**结论：必须重编 CPython 为动态链接版本。**

### 关键发现

1. **ld-musl-namespace 不是障碍**：OH 的命名空间配置文件 `ld-musl-namespace-aarch64.ini` 中，`/data/local/tmp` 走 `acquiescence` 命名空间，没有隔离限制。`LD_LIBRARY_PATH` 直接能用。**之前选静态链接是为了绕 namespace，现在发现根本不需要绕。**

2. **Rust 交叉编译 OHOS 是官方支持的**：`aarch64-unknown-linux-ohos` 是 Rust Tier 2 target，`rustup target add` 直接可用。

3. **OH 官方有 CPython 交叉编译 patch**：`third_party/python/patches/cross_compile_support_ohos.patch`，修了 `config.sub` 识别 `ohos`。

### 编译过程

#### 编译产物

| 组件 | 版本 | 产物 | 说明 |
|------|------|------|------|
| OpenSSL | 3.3.2 | `build/openssl-shared/lib/libssl.so.3 + libcrypto.so.3` | **必须编 shared**，static .a 链进 .so 时 segfault |
| zlib | 1.3.1 | `build/zlib-pic/lib/libz.a` | 加了 `-fPIC` |
| CPython | 3.11.11 | `build/python-dynamic/` | `--enable-shared`，64 个 .so 扩展模块 |

#### 踩坑记录

| 坑 | 原因 | 解法 |
|---|---|---|
| `getaddrinfo` 检测失败 | 交叉编译不能运行目标程序 | `ac_cv_buggy_getaddrinfo=no` |
| setup.py 禁了 `_socket`, `zlib`, `binascii` | OH patch 的 `DISABLED_MODULE_LIST` | 改成只禁 `_uuid` |
| 所有 .so 模块 "Failed to build" | setup.py 交叉编译时在宿主机 import aarch64 .so 当然失败 | **假报错**，.so 文件实际上都编好了 |
| OpenSSL static .a 链进 .so → segfault | 不是 PIC 问题，是模块化加载的 ABI 问题 | **OpenSSL 必须编 shared（.so）** |
| `_sha256`, `_sha1`, `_sha3` 等 segfault | Setup.stdlib 编的模块不经过 setup.py，没链 `-lpython3.11` | Makefile 的 `BLDSHARED` 加 `-L. -lpython3.11` |
| `_md5`, `_blake2` OK 但 `_sha256` crash | setup.py 编的模块有 OH patch 加的 `-lpython3.11`，Makefile 编的没有 | 同上 |

#### 核心 Bug：.so 模块必须链接 libpython

**这是最关键的发现。** CPython 3.11 的 C 扩展模块在 Linux 上通常不需要显式链接 libpython（因为符号由 python 进程自己导出）。但在 OHOS 上，**musl 的 dlopen 不会自动从调用者继承符号**——每个 .so 必须显式声明自己的依赖。

修复方法：在 Makefile 里把 `BLDSHARED` 改成：
```
BLDSHARED = $(CC) -shared $(PY_CORE_LDFLAGS) -L. -lpython3.11
```

### 板子上验证通过 ✅

```
$ LD_LIBRARY_PATH=/data/local/tmp/lib \
  PYTHONHOME=/data/local/tmp/python-home \
  SSL_CERT_FILE=/data/local/tmp/cacert.pem \
  /data/local/tmp/python-home/bin/python3.11 test_dynamic.py

Python 3.11.11 [Clang 15.0.4]
math.pi = 3.141592653589793
json OK
_struct OK
select OK
ssl: OpenSSL 3.3.2 3 Sep 2024
socket OK
ALL DYNAMIC IMPORTS OK!
```

DeepSeek LLM API 调用成功 ✅

### 部署结构（动态版）

```
/data/local/tmp/
├── python-home/
│   ├── bin/python3.11              # 4.8KB 动态链接 launcher
│   └── lib/python3.11/
│       ├── lib-dynload/            # 64 个 .so 扩展模块
│       │   ├── _ssl.cpython-311-aarch64-linux-ohos.so
│       │   ├── _json.cpython-311-aarch64-linux-ohos.so
│       │   ├── math.cpython-311-aarch64-linux-ohos.so
│       │   └── ...
│       ├── encodings/
│       ├── email/
│       ├── http/
│       └── ...
├── lib/
│   ├── libpython3.11.so.1.0       # 4.8MB Python 主库
│   ├── libpython3.11.so → ...
│   ├── libcrypto.so.3              # 4.2MB OpenSSL
│   ├── libssl.so.3                 # 836KB OpenSSL
│   └── 软链 ...
├── cacert.pem                      # Mozilla CA 证书包
├── test_llm.py
└── test_dynamic.py
```

### 运行命令模板

```bash
LD_LIBRARY_PATH=/data/local/tmp/lib \
PYTHONHOME=/data/local/tmp/python-home \
SSL_CERT_FILE=/data/local/tmp/cacert.pem \
/data/local/tmp/python-home/bin/python3.11 xxx.py
```

### 持久化文件

```
LangChain/
├── sources/
│   ├── Python-3.11.11.tgz          # CPython 源码包
│   ├── Python-3.11.11/             # 打过 OH patch 的源码
│   ├── openssl-3.3.2.tar.gz
│   ├── openssl-3.3.2/
│   └── zlib-1.3.1.tar.gz
├── build/
│   ├── python-dynamic/             # make install 产物（完整部署包）
│   ├── openssl-shared/             # OpenSSL shared 库
│   ├── zlib-pic/                   # zlib with -fPIC
│   ├── openssl/                    # （旧）静态 OpenSSL
│   ├── zlib/                       # （旧）静态 zlib
│   ├── libffi/                     # libffi
│   ├── ohos-cc.sh                  # 编译器 wrapper
│   ├── ohos-cxx.sh
│   └── python-host.tar.gz          # 宿主机 Python bootstrap
├── scripts/
│   ├── configure-dynamic.sh        # CPython 动态版 configure 脚本
│   ├── build-openssl-shared.sh     # OpenSSL shared 编译脚本
│   └── build-openssl-pic.sh        # OpenSSL PIC 编译脚本（没用上）
└── DevHistory.md
```

---

## 2026-03-11 19:56 pydantic-core Rust 交叉编译

### 背景

LangChain v0.3+ → pydantic v2 → pydantic-core（Rust .so）。这是"99% 的人卡住的地方"——PyPI 上没有 ohos 平台的 wheel，pip install 只能装到 LangChain 0.0.x 远古版本。

### 环境准备

| 组件 | 版本 | 说明 |
|------|------|------|
| Rust | 1.94.0 | `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \| sh` |
| OHOS target | aarch64-unknown-linux-ohos | `rustup target add`，Rust Tier 2 官方支持 |
| maturin | 1.12.6 | `pip3 install maturin`，Python Rust 扩展构建工具 |
| pydantic-core | 2.41.5 | `git clone --depth 1 https://github.com/pydantic/pydantic-core.git` |

### cargo 配置

```toml
# ~/.cargo/config.toml
[target.aarch64-unknown-linux-ohos]
linker = "/mnt/c/Users/lmxxf/openclaw_on_openharmony/LangChain/build/ohos-cc.sh"
```

### 编译命令

```bash
cd sources/pydantic-core

RUSTFLAGS="-L /mnt/c/Users/lmxxf/openclaw_on_openharmony/LangChain/build/python-dynamic/data/local/tmp/python-home/lib -l python3.11" \
maturin build --target aarch64-unknown-linux-ohos --release --interpreter python3.11 --skip-auditwheel
```

### 编译产物

| 产物 | 大小 | 说明 |
|------|------|------|
| `_pydantic_core.cpython-311-aarch64-linux-ohos.so` | 4.4MB | Rust 编译的核心引擎 |
| `pydantic_core/__init__.py` + `core_schema.py` | 纯 Python | 包装层 |

wheel 路径：`sources/pydantic-core/target/wheels/pydantic_core-2.41.5-cp311-cp311-linux_aarch64.whl`

### 踩坑记录

| 坑 | 原因 | 解法 |
|---|---|---|
| 第一次编译 `maturin build` 报 `Invalid python interpreter version` | 宿主机是 Python 3.10，没指定目标版本 | `--interpreter python3.11` |
| import 报 `PyInterpreterState_Get: symbol not found` | **OHOS musl dlopen 老朋友**——.so 没链 libpython，musl 不继承进程符号 | `RUSTFLAGS="-L <libpython路径> -l python3.11"` |
| maturin 报 `Failed to execute 'patchelf'` | maturin 想把依赖库打包进 wheel（auditwheel 修复），板子上已有这些库不需要 | `--skip-auditwheel` |

### 核心发现：OHOS 上所有 Python C/Rust 扩展都必须显式链接 libpython

这是第二次碰到同一个根因了（第一次是 CPython 自己的 .so 模块）。**在 OHOS 上，无论是 C 扩展还是 Rust 扩展，编出来的 .so 必须显式声明依赖 libpython3.11.so。** 这是 OHOS musl 的 dlopen 行为决定的，和标准 Linux glibc 不同。

以后编 cryptography 等其他 Rust 扩展时，同样需要加这个 RUSTFLAGS。

### 板子上验证通过 ✅

```
$ LD_LIBRARY_PATH=/data/local/tmp/lib \
  PYTHONHOME=/data/local/tmp/python-home \
  /data/local/tmp/python-home/bin/python3.11 test_pydantic.py

Python: 3.11.11 [Clang 15.0.4]
pydantic_core imported OK!
VERSION: 2.41.5
```

### 依赖补充

pydantic_core 运行时需要 `typing_extensions`（纯 Python），直接下载 .py 丢进 site-packages 即可。

### 部署更新

```
/data/local/tmp/
├── python-home/
│   └── lib/python3.11/
│       ├── site-packages/
│       │   ├── pydantic_core/
│       │   │   ├── __init__.py
│       │   │   ├── _pydantic_core.cpython-311-aarch64-linux-ohos.so  # 4.4MB
│       │   │   ├── core_schema.py
│       │   │   └── _pydantic_core.pyi
│       │   ├── pydantic_core-2.41.5.dist-info/
│       │   └── typing_extensions.py
│       ├── lib-dynload/          # 64 个 CPython .so
│       └── ...
├── lib/
│   ├── libpython3.11.so.1.0
│   ├── libcrypto.so.3
│   ├── libssl.so.3
│   └── ...
└── cacert.pem
```

---

## 2026-03-11 20:22 LangChain 全链路打通 🔥

### 依赖链分析

langchain-core 1.2.18 的完整依赖树：

**需要交叉编译的（C/Rust 扩展）：**

| 包 | 版本 | 底层 | 编译方式 |
|---|---|---|---|
| pydantic-core | 2.41.5 | Rust/maturin | 同模板 |
| uuid-utils | 0.14.1 | Rust/maturin | 同模板 |
| jiter | 0.13.0 | Rust/maturin | 同模板 |
| xxhash | 3.6.0 | C | 手动 clang 编译（2 个 .c 文件） |

**有纯 Python fallback 不用编的：**

| 包 | 说明 |
|---|---|
| orjson | langsmith 有完整 `json` fallback（`_orjson.py`） |
| zstandard | langsmith 压缩传输用，没有就不压缩 |
| pyyaml | 没有 libyaml 自动用纯 Python 解析器 |
| charset-normalizer | requests 能 fallback |

**纯 Python 包（直接丢进去）：**

pydantic, langchain-core, langsmith, openai, langchain-openai, httpx, httpcore, anyio, requests, requests-toolbelt, urllib3, certifi, idna, h11, sniffio, jsonpatch, jsonpointer, packaging, tenacity, typing-extensions, typing-inspection, annotated-types, distro, tqdm

### 额外的 Rust 交叉编译

uuid-utils 和 jiter 都是 maturin 项目，和 pydantic-core 完全一样的模板：

```bash
# uuid-utils
cd sources/uuid-utils
RUSTFLAGS="-L <libpython路径> -l python3.11" \
maturin build --target aarch64-unknown-linux-ohos --release --interpreter python3.11 --skip-auditwheel

# jiter
cd sources/jiter/crates/jiter-python
RUSTFLAGS="-L <libpython路径> -l python3.11" \
maturin build --target aarch64-unknown-linux-ohos --release --interpreter python3.11 --skip-auditwheel
```

### xxhash C 扩展手动编译

xxhash 是 C 扩展（setup.py），源码内嵌了 xxhash.c，一行命令编完：

```bash
$CC -shared -fPIC -O2 \
  -I$PYTHON_INC -I sources/xxhash/deps/xxhash \
  -L$PYTHON_LIB -lpython3.11 \
  -o _xxhash.cpython-311-aarch64-linux-ohos.so \
  sources/xxhash/src/_xxhash.c sources/xxhash/deps/xxhash/xxhash.c
```

注意：`.so` 要放到 `xxhash/` 包目录内（不是 site-packages 根目录），因为包内 `from xxhash._xxhash import ...`。

### 踩坑记录

| 坑 | 原因 | 解法 |
|---|---|---|
| `No module named 'typing_extensions'` | pydantic_core 运行时依赖 | 下载 .py 丢进 site-packages |
| `No module named 'typing_inspection'` | pydantic v2 新增依赖 | 同上 |
| `No module named 'annotated_types'` | pydantic v2 依赖 | 同上 |
| `No module named '_sysconfigdata__linux_aarch64-linux-ohos'` | 之前推标准库时漏了这个文件 | 从 build 产物里补推 |
| `No module named 'xxhash._xxhash'` | .so 放错目录（在 site-packages 根目录） | 移到 `xxhash/` 包目录内 |
| `No module named 'tiktoken'` | langchain-openai 硬依赖 tiktoken（Rust 扩展） | 绕过：直接用 openai SDK + langchain-core 消息类型 |
| API key 401 | 用了旧 key | 换正确的 key |

### 板子上验证通过 ✅✅✅

```
$ LD_LIBRARY_PATH=/data/local/tmp/lib \
  PYTHONHOME=/data/local/tmp/python-home \
  SSL_CERT_FILE=/data/local/tmp/cacert.pem \
  /data/local/tmp/python-home/bin/python3.11 test_langchain_llm.py

Python: 3.11.11 [Clang 15.0.4]
langchain-core: OK
openai SDK: OK
Calling DeepSeek via OpenAI SDK...
Response: Hello, it's a pleasure to meet you.
Type: AIMessage

=== LangChain + OpenAI SDK on OpenHarmony: SUCCESS ===
```

**全链路：OH RK3568 → Python 3.11 → pydantic 2.12.5 → langchain-core 1.2.18 → openai SDK → HTTPS/TLS → DeepSeek API → AIMessage**

### 最终部署结构

```
/data/local/tmp/
├── python-home/
│   ├── bin/python3.11
│   └── lib/python3.11/
│       ├── site-packages/
│       │   ├── pydantic_core/          # Rust .so (4.4MB)
│       │   ├── pydantic/               # 纯 Python
│       │   ├── langchain_core/         # 纯 Python
│       │   ├── langchain_openai/       # 纯 Python
│       │   ├── openai/                 # 纯 Python
│       │   ├── langsmith/              # 纯 Python
│       │   ├── uuid_utils/             # Rust .so
│       │   ├── jiter/                  # Rust .so
│       │   ├── xxhash/                 # C .so
│       │   │   └── _xxhash.cpython-311-aarch64-linux-ohos.so
│       │   ├── httpx/                  # 纯 Python
│       │   ├── httpcore/               # 纯 Python
│       │   ├── requests/               # 纯 Python
│       │   ├── typing_extensions.py
│       │   ├── typing_inspection/
│       │   ├── annotated_types/
│       │   └── ...（其他纯 Python 包）
│       ├── lib-dynload/                # 64 个 CPython .so
│       ├── _sysconfigdata__linux_aarch64-linux-ohos.py
│       └── ...
├── lib/
│   ├── libpython3.11.so.1.0
│   ├── libcrypto.so.3
│   ├── libssl.so.3
│   └── ...
└── cacert.pem
```

### 完成状态

- [x] OpenSSL 3.3.2 交叉编译（shared .so）
- [x] zlib 1.3.1 交叉编译（-fPIC）
- [x] CPython 3.11.11 动态链接版（64 个 .so 模块）
- [x] pydantic-core 2.41.5（Rust 交叉编译）
- [x] uuid-utils 0.14.1（Rust 交叉编译）
- [x] jiter 0.13.0（Rust 交叉编译）
- [x] xxhash 3.6.0（C 交叉编译）
- [x] LangChain 全依赖链安装（纯 Python 包 + 上述扩展）
- [x] **DeepSeek API 通过 LangChain + OpenAI SDK 调用成功**

### Rust 交叉编译通用模板

```bash
# 1. cargo config（一次性）
# ~/.cargo/config.toml 已配好 linker

# 2. 编译命令模板
RUSTFLAGS="-L <libpython3.11.so所在目录> -l python3.11" \
maturin build \
  --target aarch64-unknown-linux-ohos \
  --release \
  --interpreter python3.11 \
  --skip-auditwheel

# 3. 产物在 target/wheels/*.whl，解压后把包目录丢进 site-packages
```

---

## 2026-03-12 部署包整理

### 背景

之前编译产物散落在 `build/` 各处，推板子时是手动一坨一坨 hdc 推的，打包也有重叠有遗漏。README 的"快速部署"引用的目录全在 .gitignore 里——clone 下来根本跑不通。

### 整理

把所有编译产物合并成一个完整部署包 `build/langchain-ohos-deploy.tar.gz`（25MB），提交进 git。

**打包内容：**

| 内容 | 说明 |
|------|------|
| `python-home/bin/python3.11` | 动态链接 CPython 解释器 |
| `python-home/lib/python3.11/` | 标准库（去掉 test/、idlelib、tkinter 等，57MB→压缩15MB） |
| `python-home/lib/python3.11/site-packages/` | LangChain 全依赖链（纯 Python + Rust/C 扩展） |
| `python-home/lib/libpython3.11.so.1.0` | Python 主库 4.8MB |
| `lib/libssl.so.3 + libcrypto.so.3` | OpenSSL 5.1MB |
| pydantic-core, jiter, uuid-utils, xxhash | 4 个交叉编译的 Rust/C .so |

**清理掉的垃圾：**
- `python-home/lib/python3.11/test/`（122MB）
- `idlelib/`, `tkinter/`, `turtledemo/`, `lib2to3/`, `ensurepip/`
- `_test*.so`, `xx*.so`（测试用模块）
- 全部 `__pycache__/`

**合并的散装包：**
- `pydantic_core_ohos_v2.tar.gz` → 合入
- `langchain-deps.tar.gz` / `langchain-deps2.tar.gz` → 合入（两个是重复的）
- `deps-batch2.tar.gz` → 合入
- `deps-batch3.tar.gz` → 合入
- `typing_inspection.tar.gz` → 合入

**修复：** xxhash 的 `_xxhash.so` 从 site-packages 根目录移到 `xxhash/` 包目录内。

---

## 2026-03-12 AgentScope 移植（交叉编译阶段完成，待板子验证）

### 背景

LangChain 跑通后，接到新需求：移植 AgentScope（阿里的 AI Agent 框架）到 OH。AgentScope 是纯 Python 项目，但依赖链里有几个 Rust/C 原生扩展。

### 依赖链分析

AgentScope 1.0.16 的依赖树分三类：

**已有（LangChain 时编好的，直接复用）：**
- pydantic 2.12.5 + pydantic-core 2.41.5、openai SDK、httpx、jiter、uuid-utils 等 ~20 个包

**需要交叉编译的（4 个新增）：**

| 包 | 类型 | 构建方式 | 产物大小 |
|---|---|---|---|
| tiktoken 0.12.0 | Rust/PyO3 | cargo build --features python | 2.4MB |
| cryptography 47.0.0 | Rust/maturin + OpenSSL | maturin build + OPENSSL_DIR | 6.0MB |
| regex | C | 手动 clang（2 个 .c 文件） | 734KB |
| cffi 1.17.1 | C + libffi | 手动 clang（1 个 .c 文件） | 261KB |

**可绕过或有纯 Python fallback 的：**

| 包 | 处理方式 |
|---|---|
| numpy | stub（只有音频和 embedding 缓存用到，核心 Agent 功能不需要） |
| aiohttp/multidict/yarl/frozenlist/propcache | PyPI 有纯 Python wheel（`py3-none-any`） |
| protobuf | PyPI 有纯 Python wheel |
| sqlalchemy/wrapt | PyPI 有纯 Python wheel |
| grpcio | 不用 gRPC OTLP 导出器，走 HTTP 版 |
| sounddevice | 不用音频就不需要 |
| rpds-py | 写了最小 stub（HashTrieMap/HashTrieSet），jsonschema 基本功能可用 |

**纯 Python 包（~35 个，直接下载解压）：**
agentscope、anthropic、dashscope、mcp、aioitertools、aiofiles、json5、json_repair、opentelemetry-api/sdk/exporter、starlette、uvicorn、pydantic-settings、python-socketio、shortuuid、jsonschema、PyJWT 等

### 编译过程

#### tiktoken（Rust/PyO3，setuptools-rust）

tiktoken 用 setuptools-rust 不用 maturin，但可以直接用 cargo：

```bash
cd sources/tiktoken
RUSTFLAGS="-L <libpython路径> -l python3.11" \
PYO3_CROSS_PYTHON_VERSION=3.11 \
cargo build --target aarch64-unknown-linux-ohos --release --features python
# 产物：target/aarch64-unknown-linux-ohos/release/libtiktoken.so
# 重命名为 _tiktoken.cpython-311-aarch64-linux-ohos.so，放到 tiktoken/ 包目录
```

一次过，35 秒。

#### cryptography（Rust workspace + CFFI + OpenSSL）

最复杂的一个。cryptography 是 Rust workspace（8 个子 crate），还有一个 CFFI 生成的 C 绑定层。

**踩坑记录：**

| 坑 | 原因 | 解法 |
|---|---|---|
| `_openssl.o is incompatible with aarch64linux` | 第一次没设 `CC_aarch64_unknown_linux_ohos`，`cc` crate 用了宿主机 gcc 编 C 代码 | 设 `CC_aarch64_unknown_linux_ohos=ohos-cc.sh` |
| `pyconfig.h file not found` | `cc` crate 找的是宿主机 Python 3.10 头文件 | `CFLAGS_aarch64_unknown_linux_ohos="-I<OHOS Python include>"` |
| `linker cc not found` | PATH 被覆盖导致宿主机 `/usr/bin/cc` 丢失 | 不用 PATH 覆盖，用环境变量指定 |
| `No module named 'cffi'` | build.rs 调用宿主机 python 运行 CFFI 生成器，缺 cffi 模块 | `/tmp/python-host/bin/pip3.11 install cffi setuptools` |
| `Header expansion error` OpenSSL | `OPENSSL_DIR` 环境变量 `&&` 截断 | 一行写所有环境变量，不用 `&&` |

**最终编译命令：**

```bash
cd sources/cryptography
CC_aarch64_unknown_linux_ohos="<ohos-cc.sh路径>" \
CXX_aarch64_unknown_linux_ohos="<ohos-cxx.sh路径>" \
CFLAGS_aarch64_unknown_linux_ohos="-I<OHOS Python 3.11 include>" \
OPENSSL_DIR=<openssl-shared路径> \
OPENSSL_LIB_DIR=<openssl-shared/lib> \
OPENSSL_INCLUDE_DIR=<openssl-shared/include> \
OPENSSL_STATIC=0 \
OPENSSL_NO_VENDOR=1 \
RUSTFLAGS="-L <libpython路径> -l python3.11" \
PYO3_CROSS_PYTHON_VERSION=3.11 \
PYO3_CROSS_LIB_DIR=<libpython路径> \
PYO3_PYTHON=/tmp/python-host/bin/python3.11 \
maturin build --target aarch64-unknown-linux-ohos --release --interpreter /tmp/python-host/bin/python3.11 --skip-auditwheel
```

33 秒编完。cryptography 的关键经验：**交叉编译 Rust+C 混合项目时，宿主机需要能运行 build script（需要 `cc`、`python3`、`cffi`），目标平台需要 `CC_<target>` 指定交叉编译器。两套工具链不能混。**

#### regex（C，2 个源文件）

```bash
$OHOS_CC -shared -fPIC -O2 \
  -I$PYTHON_INC -L$PYTHON_LIB -lpython3.11 \
  sources/regex-src/src/_regex.c sources/regex-src/src/_regex_unicode.c \
  -o regex/_regex.cpython-311-aarch64-linux-ohos.so
```

一次过。

#### cffi（C，1 个源文件 + libffi）

```bash
$OHOS_CC -shared -fPIC -O2 \
  -DFFI_BUILDING=1 \
  -I$PYTHON_INC -I$LIBFFI_INC -I<cffi_src/src/c> \
  -L$PYTHON_LIB -lpython3.11 -L$LIBFFI_LIB -lffi \
  <cffi_src>/src/c/_cffi_backend.c \
  -o _cffi_backend.cpython-311-aarch64-linux-ohos.so
```

一次过。libffi 是静态链接的（`.a`），不需要额外 `.so`。

### 编译产物

| 产物 | 位置 | 大小 |
|------|------|------|
| tiktoken .so | `sources/tiktoken/_tiktoken.cpython-311-aarch64-linux-ohos.so` | 2.4MB |
| cryptography wheel | `sources/cryptography/target/wheels/cryptography-47.0.0.dev1-cp311-abi3-linux_aarch64.whl` | 2.0MB |
| regex .so | `sources/_regex.cpython-311-aarch64-linux-ohos.so` | 734KB |
| cffi .so | `_cffi_backend.cpython-311-aarch64-linux-ohos.so`（项目根目录） | 261KB |
| AgentScope 完整依赖包 | `/tmp/agentscope-ohos-deps.tar.gz` | 6.5MB |

### 部署方式

AgentScope 依赖包是增量包，叠加在 LangChain 部署包之上：

```bash
# 先部署 LangChain 基础包（CPython + 基础依赖）
tar xzf build/langchain-ohos-deploy.tar.gz -C /data/local/tmp/

# 再叠加 AgentScope 依赖
tar xzf agentscope-ohos-deps.tar.gz -C /data/local/tmp/python-home/lib/python3.11/site-packages/
```

### 板子验证（部分通过）

已推到板子并验证。部署包 `build/agentscope-ohos-deps.tar.gz`（6.5MB）已提交 git。

**注意**：OH 的 tar 不接受 `./` 前缀路径，需要用 `*` 打包而不是 `.`。

**import 测试结果：**

| 模块 | 状态 | 说明 |
|------|------|------|
| cryptography 47.0.0 | ✅ | Rust+OpenSSL 在 OHOS musl 上正常 |
| regex 2026.2.28 | ✅ | C 扩展正常 |
| cffi 1.17.1 | ✅ | C 扩展正常，libffi 静态链入 |
| tiktoken | ✅ | 需要补 `tiktoken_ext/` 目录（源码里有，打包时漏了） |
| jsonschema/referencing | ✅ | rpds stub v3 解决（需要 `convert()` 类方法 + `HashTrieSet.update()`） |
| agentscope | ❌ | 卡在缺 `sqlalchemy`（纯 Python wheel 没装进去） |

**踩坑记录（板子验证阶段）：**

| 坑 | 原因 | 解法 |
|---|---|---|
| OH tar 拒绝解压 | tar.gz 内路径以 `./` 开头，OH tar 报 `not under target` | 用 `tar czf xxx.tar.gz *` 打包（不带 `./` 前缀） |
| `import agentscope` → numpy ImportError | numpy stub 直接在 `__init__.py` 里 raise | 改成 import 时只 warn，具体函数调用时才 raise |
| `No module named 'tiktoken_ext'` | tiktoken 源码的 `tiktoken_ext/` 目录没打进依赖包 | 从 `sources/tiktoken/tiktoken_ext/` 补推 |
| rpds `HashTrieMap.convert` 缺失 | referencing 库需要 `convert()` 类方法 | rpds stub v2 添加 |
| rpds `HashTrieSet.update` 缺失 | referencing `Registry.combine()` 调用 set.update() | rpds stub v3：改 frozenset→set，加 update() |
| 缺 sqlalchemy | 纯 Python wheel 下载了但没装进 site-packages | **待修复：下一个 session 补装** |

**rpds stub 最终版（v3）要点：**
- `HashTrieMap` 继承 `dict`，加 `convert()` 类方法
- `HashTrieSet` 继承 `set`（不能用 frozenset，需要 update），加 `convert()`
- `List` 继承 `tuple`，加 `convert()`、`push_front()`、`drop_first()`
- 这三个类方法 + update 方法足以支撑 referencing + jsonschema 启动

### 源码位置

```
sources/
├── tiktoken/              # git clone --depth 1
├── cryptography/          # git clone --depth 1
├── sources/regex-src/     # git clone --depth 1（多了一层 sources/，clone 时 cwd 不对）
└── _regex.cpython-311-aarch64-linux-ohos.so  # regex 编译产物
```

## 2026-03-12 21:52 AgentScope 板子验证完成 ✅

### 补装的缺失包

上一个 session 编译完成但 `import agentscope` 还缺若干纯 Python 包。本次逐个补装：

| 包 | 版本 | 来源 | 说明 |
|---|---|---|---|
| sqlalchemy | 2.0.41 | PyPI py3-none-any wheel | 纯 Python（greenlet 可选，不装） |
| multidict | 6.7.1 | PyPI py3-none-any wheel | aiohttp 依赖 |
| yarl | 1.23.0 | PyPI py3-none-any wheel | aiohttp 依赖 |
| frozenlist | 1.8.0 | PyPI py3-none-any wheel | aiohttp/aiosignal 依赖 |
| propcache | 0.4.1 | PyPI py3-none-any wheel | yarl 依赖 |
| async_timeout | 5.0.1 | PyPI py3-none-any wheel | aiohttp 依赖 |
| loguru | 0.7.3 | PyPI py3-none-any wheel | agentscope 日志 |
| bidict | 0.23.1 | PyPI py3-none-any wheel | python-socketio 硬依赖 |
| python-engineio | 4.13.1 | PyPI py3-none-any wheel | python-socketio 依赖 |
| importlib-metadata | 8.7.1 | PyPI py3-none-any wheel | opentelemetry 需要 |

### aiohttp 3.6.2 不兼容 Python 3.11

PyPI 上 aiohttp 最后一个纯 Python wheel 是 3.6.2（Python 3.8 时代），和 Python 3.11 完全不兼容：
- `asyncio.coroutines._DEBUG` 被删除
- `@asyncio.coroutine` 装饰器被删除

但 agentscope 本身不直接用 aiohttp——是 **dashscope SDK** 在顶层 `import aiohttp`。

**解法：替换为 aiohttp stub 模块。** 只提供 dashscope 导入时需要的符号（`ClientSession`、`ClientTimeout`、`WSMsgType` 等），实际调用时抛 `NotImplementedError`。dashscope 的同步 API 走 requests，不需要 aiohttp。

### 板子验证通过 ✅

```
$ LD_LIBRARY_PATH=/data/local/tmp/lib \
  PYTHONHOME=/data/local/tmp/python-home \
  SSL_CERT_FILE=/data/local/tmp/cacert.pem \
  /data/local/tmp/python-home/bin/python3.11 test_agentscope2.py

Python: 3.11.11 [Clang 15.0.4]
agentscope: 1.0.16

DeepSeek response: Hello from OpenHarmony—embracing a future of seamless, intelligent connectivity across all devices!
Tokens: 16 in, 19 out

=== AgentScope 1.0.16 + DeepSeek on OpenHarmony 6.0: SUCCESS ===
```

**全链路：OH RK3568 → Python 3.11.11 → AgentScope 1.0.16 → OpenAI SDK (async) → HTTPS/TLS → DeepSeek API → ChatResponse**

### AgentScope 1.0.16 API 注意事项

- `agentscope.init()` 不再接受 `model_configs` 参数（旧版 API）
- Model 类在 `agentscope.model._openai_model.OpenAIChatModel`（不是旧版的 `OpenAIChatWrapper`）
- `__call__` 是 **async**，需要 `asyncio.run()` 包裹
- 参数名：`client_kwargs`（不是 `client_args`）、`generate_kwargs`（不是 `generate_args`）
- Response 是 `ChatResponse`，`.content` 是 block list：`response.content[0]["text"]`

### 可选的缺失模块（不影响核心功能）

full import scan 发现的可选缺失模块，都是 try/except 保护的，不影响 `import agentscope`：

| 模块 | 类型 | 说明 |
|---|---|---|
| `_bz2`, `_lzma` | CPython 未编 | 压缩格式，不影响功能 |
| `greenlet` | C 扩展 | sqlalchemy async 用，同步模式不需要 |
| `brotli/brotlicffi` | C 扩展 | HTTP 压缩，可选 |
| `pygments` | 纯 Python | 语法高亮，可选 |
| `tornado` | 混合 | WebSocket 备选，不用 |
| `fqdn`, `rfc3987*`, `rfc3339_validator`, `webcolors`, `uri_template`, `isoduration` | 纯 Python | jsonschema format 验证器，可选 |

---

## 新 Session 恢复指南

**下次开 session 时让朱雀读这个文件，就能接上。**

### 最终目标

1. **LangChain on OH** ✅ 已完成。
2. **AgentScope on OH** ✅ 已完成。

### 当前状态

**LangChain**：全链路已跑通。部署包 `build/langchain-ohos-deploy.tar.gz`（25MB）已提交 git。

**AgentScope**：全链路已跑通。`agentscope.init()` + `OpenAIChatModel` + DeepSeek API 调用成功。

### 待整理

1. 把补装的包重新打包进 `build/agentscope-ohos-deps-v2.tar.gz`
2. aiohttp stub 需要打进包里（目前只在板子上手动创建的）

### 运行命令模板

```bash
LD_LIBRARY_PATH=/data/local/tmp/lib \
PYTHONHOME=/data/local/tmp/python-home \
SSL_CERT_FILE=/data/local/tmp/cacert.pem \
/data/local/tmp/python-home/bin/python3.11 xxx.py
```

### Rust/C 交叉编译模板

**Rust (maturin 项目)：**
```bash
CC_aarch64_unknown_linux_ohos="<ohos-cc.sh>" \
RUSTFLAGS="-L <libpython路径> -l python3.11" \
PYO3_CROSS_PYTHON_VERSION=3.11 \
maturin build --target aarch64-unknown-linux-ohos --release --interpreter python3.11 --skip-auditwheel
```

**Rust (setuptools-rust / cargo 项目)：**
```bash
RUSTFLAGS="-L <libpython路径> -l python3.11" \
PYO3_CROSS_PYTHON_VERSION=3.11 \
cargo build --target aarch64-unknown-linux-ohos --release --features python
# 产物 libtiktoken.so → 重命名为 _tiktoken.cpython-311-aarch64-linux-ohos.so
```

**C 扩展（手动编译）：**
```bash
$OHOS_CC -shared -fPIC -O2 \
  -I$PYTHON_INC -L$PYTHON_LIB -lpython3.11 \
  source.c -o _module.cpython-311-aarch64-linux-ohos.so
```

**cryptography（Rust+C+OpenSSL，最复杂模板）：**
需要额外设置 `CC_aarch64_unknown_linux_ohos`、`CFLAGS_aarch64_unknown_linux_ohos`、`OPENSSL_DIR/LIB_DIR/INCLUDE_DIR`、`PYO3_PYTHON`（指向宿主机 python3.11，需要 cffi+setuptools）。详见上方编译记录。

---

*OH 工程路径: `/home/lmxxf/oh6/source`*
*GitHub: https://github.com/lmxxf/langchain-on-openharmony*
*项目路径: `/home/lmxxf/work/openclaw_on_openharmony/LangChain/`*
*LangChain 源码: `sources/pydantic-core/`, `sources/uuid-utils/`, `sources/jiter/`, `sources/xxhash/`*
*AgentScope 源码: `sources/tiktoken/`, `sources/cryptography/`, `sources/sources/regex-src/`*
*AgentScope 依赖包: `build/agentscope-ohos-deps.tar.gz`（6.5MB，已提交 git）+ 补丁包 `build/agentscope-fix.tar.gz`*
*板子上的修复脚本: `/data/local/tmp/fix_rpds3.py`（rpds stub v3）、`/data/local/tmp/fix_aiohttp_stub.py`（aiohttp stub）*
*API key: `.api-key` 文件*
