# LangChain on OpenHarmony 6.0 (aarch64)

在 RK3568（OpenHarmony 6.0）上运行最新版 LangChain。

## 当前进度

| 阶段 | 状态 | 说明 |
|------|------|------|
| CPython 3.11.11 交叉编译（动态链接） | ✅ | 64 个 .so 扩展模块，dlopen 正常 |
| OpenSSL 3.3.2（shared） | ✅ | libssl.so.3 + libcrypto.so.3 |
| HTTPS + TLS | ✅ | CA 证书包，ssl/socket 模块 |
| DeepSeek LLM API | ✅ | 全链路：OH → Python → HTTPS → LLM → JSON |
| pydantic-core（Rust 交叉编译） | 🔲 | 下一步 |
| LangChain hello world | 🔲 | |

## 前提条件

- OpenHarmony 源码树（需要 Clang 15 工具链和 musl sysroot）
- Linux 宿主机（WSL2 / Ubuntu 均可）
- `wget`、`tar`、`make`、`perl`（编 OpenSSL 需要）
- RK3568 开发板，通过 hdc 连接

## 快速部署（不重编）

板子上的部署结构：

```
/data/local/tmp/
├── python-home/
│   ├── bin/python3.11              # 4.8KB 动态链接 launcher
│   └── lib/python3.11/
│       ├── lib-dynload/            # 64 个 .so 扩展模块
│       ├── encodings/
│       ├── email/ http/ urllib/ json/ ...
│       └── ssl.py
├── lib/
│   ├── libpython3.11.so.1.0       # 4.8MB Python 主库
│   ├── libcrypto.so.3              # 4.2MB OpenSSL
│   ├── libssl.so.3                 # 836KB OpenSSL
│   └── 软链（libpython3.11.so, libpython3.so, libcrypto.so, libssl.so）
└── cacert.pem                      # Mozilla CA 证书包
```

运行命令：

```bash
LD_LIBRARY_PATH=/data/local/tmp/lib \
PYTHONHOME=/data/local/tmp/python-home \
SSL_CERT_FILE=/data/local/tmp/cacert.pem \
/data/local/tmp/python-home/bin/python3.11 your_script.py
```

## 从零重编

### 环境变量

```bash
OH_SRC=/home/lmxxf/oh6/source
OH_CLANG=$OH_SRC/prebuilts/clang/ohos/linux-x86_64/llvm/bin
PROJ_DIR=$(pwd)  # LangChain/ 目录
```

### Step 0: 编译器 Wrapper

```bash
cat > /tmp/ohos-cc.sh << 'EOF'
#!/bin/bash
exec $OH_CLANG/clang \
  --target=aarch64-unknown-linux-ohos \
  --sysroot=$OH_SRC/prebuilts/ohos-sdk/linux/20/native/sysroot \
  "$@"
EOF
# ohos-cxx.sh 同理，把 clang 改成 clang++
chmod +x /tmp/ohos-cc.sh /tmp/ohos-cxx.sh
```

### Step 1: OpenSSL 3.3.2（必须 shared）

```bash
cd sources/openssl-3.3.2
./Configure linux-aarch64 --prefix=$PROJ_DIR/build/openssl-shared \
  shared no-tests no-ui-console \
  CC=/tmp/ohos-cc.sh AR=$OH_CLANG/llvm-ar RANLIB=$OH_CLANG/llvm-ranlib
make -j$(nproc) && make install_sw
```

> **关键**：OpenSSL 必须编成 shared（.so），不能编 static（.a）。
> static .a 链进 Python 扩展 .so 时会 segfault（OHOS musl 的 dlopen 初始化行为导致）。

### Step 2: zlib 1.3.1（需要 -fPIC）

```bash
cd sources/zlib-1.3.1
CC=/tmp/ohos-cc.sh CFLAGS="-fPIC" ./configure --prefix=$PROJ_DIR/build/zlib-pic --static
make -j$(nproc) && make install
```

### Step 3: 宿主机 Python（bootstrap）

交叉编译 CPython 需要同版本宿主机 Python。如果已有 `build/python-host.tar.gz` 直接解压到 `/tmp/python-host/`。

### Step 4: CPython 3.11.11

```bash
cd sources/Python-3.11.11

# 应用 OH 官方 patch
patch -p1 < $OH_SRC/third_party/python/patches/cross_compile_support_ohos.patch

# 手动 patch configure：在 aarch64-linux-gnu 前加 __OHOS__ 检测
# 手动 patch setup.py：DISABLED_MODULE_LIST 改为只禁 ['_uuid']

# configure
mkdir -p build-ohos-dynamic && cd build-ohos-dynamic
../configure \
  --host=aarch64-unknown-linux-ohos \
  --build=x86_64-linux-gnu \
  --with-build-python=/tmp/python-host/bin/python3.11 \
  --enable-shared \
  --prefix=/data/local/tmp/python-home \
  --with-openssl=$PROJ_DIR/build/openssl-shared \
  CC=/tmp/ohos-cc.sh CXX=/tmp/ohos-cxx.sh \
  LDFLAGS="-L... -Wl,-rpath,/data/local/tmp/lib -Wl,--dynamic-linker,/system/lib/ld-musl-aarch64.so.1" \
  ac_cv_buggy_getaddrinfo=no ac_cv_file__dev_ptmx=yes ac_cv_file__dev_ptc=no

# patch pyconfig.h（undef HAVE_LIBINTL_H, HAVE_LINUX_CAN_RAW_*）
# patch Makefile: BLDSHARED 加 -L. -lpython3.11

make -j$(nproc)
make install DESTDIR=$PROJ_DIR/build/python-dynamic
```

> **核心 Bug**：OHOS musl 的 dlopen 不会从调用者继承符号。
> 每个 .so 扩展模块必须显式链接 `-lpython3.11`，否则 segfault。
> 修复：Makefile 里 `BLDSHARED = $(CC) -shared $(PY_CORE_LDFLAGS) -L. -lpython3.11`

### Step 5: Strip + 部署

```bash
llvm-strip python-home/bin/python3.11
llvm-strip python-home/lib/libpython3.11.so.1.0
for f in python-home/lib/python3.11/lib-dynload/*.so; do llvm-strip $f; done
# hdc file send ...
```

## 踩坑总结

详见 [DevHistory.md](DevHistory.md)。核心教训：

1. **OHOS musl 的 dlopen 不继承调用者符号** —— 所有 .so 必须显式链 libpython
2. **OpenSSL 必须编 shared** —— static .a 链进 .so 时 segfault
3. **OH 的 ld-musl-namespace 对 `/data/local/tmp` 没有隔离** —— `LD_LIBRARY_PATH` 直接能用
4. **setup.py 报 "Failed to build" 是假报错** —— 交叉编译时在宿主机 import aarch64 .so 失败，文件实际已编好
5. **OH 官方有 CPython 交叉编译 patch** —— `third_party/python/patches/`

## 已验证（RK3568）

```
$ python3.11 test_dynamic.py
Python 3.11.11 [Clang 15.0.4]
math.pi = 3.141592653589793
json OK, _struct OK, select OK
ssl: OpenSSL 3.3.2 3 Sep 2024
socket OK
ALL DYNAMIC IMPORTS OK!

$ python3.11 test_llm.py
LLM response: Hello there, my new friend.
```
