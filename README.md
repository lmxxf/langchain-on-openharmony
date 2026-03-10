# CPython 3.11 for OpenHarmony 6.0 (aarch64) — 编译指南

基于 OH 自带的 Clang 15 + musl sysroot，交叉编译静态链接的 CPython 3.11.11，可在 RK3568 等 aarch64 设备上直接运行。

## 前提条件

- OpenHarmony 源码树（需要 Clang 工具链和 musl sysroot）
- Linux 宿主机（WSL2 / Ubuntu 均可）
- `wget`、`tar`、`make`、`perl`（编 OpenSSL 需要）

本文档以 OH 源码路径 `/home/lmxxf/oh6/source` 为例，实际使用时替换为你的路径。

## 目录结构

```
LangChain/
├── README.md              # 本文档
├── DevHistory.md          # 开发过程记录（踩坑日志）
├── sources/               # 源码 tarball
│   ├── Python-3.11.11.tar.xz
│   ├── openssl-3.3.2.tar.gz
│   ├── zlib-1.3.1.tar.gz
│   └── libffi-3.4.6.tar.gz
└── build/                 # 编译产物 + 配置文件
    ├── python3-ohos           # 最终二进制（11MB，strip 后）
    ├── Setup.local            # CPython 内建模块配置
    ├── configure.patched      # patch 过的 CPython configure
    ├── pyconfig.h.patched     # patch 过的 pyconfig.h
    ├── ohos-cc.sh             # C 编译器 wrapper
    ├── ohos-cxx.sh            # C++ 编译器 wrapper
    ├── python-host.tar.gz     # 宿主机 Python（bootstrap 用）
    ├── openssl/               # OpenSSL 静态库 + 头文件
    ├── zlib/                  # zlib 静态库 + 头文件
    ├── libffi/                # libffi 静态库 + 头文件
    └── python/                # CPython 标准库（make install 产物）
```

## 快速部署（不重编，直接用现有产物）

```bash
# 1. 推二进制到板子
hdc file send build/python3-ohos /data/local/tmp/python3
hdc shell 'chmod +x /data/local/tmp/python3'

# 2. 打包精简版标准库（去掉 test/idle/tkinter 等）
cd build/python
tar czf /tmp/python-stdlib.tar.gz lib/python3.11/

# 3. 推标准库到板子
hdc file send /tmp/python-stdlib.tar.gz /data/local/tmp/python-stdlib.tar.gz
hdc shell 'mkdir -p /data/local/tmp/python-home && cd /data/local/tmp/python-home && tar xzf /data/local/tmp/python-stdlib.tar.gz'

# 4. 运行
hdc shell 'PYTHONHOME=/data/local/tmp/python-home /data/local/tmp/python3 -c "import ssl; print(ssl.OPENSSL_VERSION)"'
# 输出: OpenSSL 3.3.2 3 Sep 2024
```

## 从零重编（完整流程）

以下步骤假设从干净状态开始，所有编译在 `/tmp` 下进行。

### 下载源码

```bash
mkdir -p sources && cd sources
wget https://www.python.org/ftp/python/3.11.11/Python-3.11.11.tar.xz
wget https://github.com/openssl/openssl/releases/download/openssl-3.3.2/openssl-3.3.2.tar.gz
wget https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
wget https://github.com/libffi/libffi/releases/download/v3.4.6/libffi-3.4.6.tar.gz
cd ..
```

### 环境变量

```bash
OH_SRC=/home/lmxxf/oh6/source
OH_CLANG=$OH_SRC/prebuilts/clang/ohos/linux-x86_64/llvm/bin
OH_SYSROOT=$OH_SRC/prebuilts/ohos-sdk/linux/20/native/sysroot
PROJ_DIR=/home/lmxxf/work/openclaw_on_openharmony/LangChain
BUILD_DIR=$PROJ_DIR/build
```

### Step 0: 编译器 Wrapper

CPython 的 configure 不支持 CC 带参数，用 wrapper 脚本绕过：

```bash
cat > /tmp/ohos-cc.sh << 'EOF'
#!/bin/bash
exec /home/lmxxf/oh6/source/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang \
  --target=aarch64-unknown-linux-ohos \
  --sysroot=/home/lmxxf/oh6/source/prebuilts/ohos-sdk/linux/20/native/sysroot \
  "$@"
EOF

cat > /tmp/ohos-cxx.sh << 'EOF'
#!/bin/bash
exec /home/lmxxf/oh6/source/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang++ \
  --target=aarch64-unknown-linux-ohos \
  --sysroot=/home/lmxxf/oh6/source/prebuilts/ohos-sdk/linux/20/native/sysroot \
  "$@"
EOF

chmod +x /tmp/ohos-cc.sh /tmp/ohos-cxx.sh
```

> **注意**：如果在 Windows 环境下创建脚本，务必确保是 Unix 换行符（LF），否则会报 `bad interpreter: ^M`。修复：`sed -i 's/\r$//' /tmp/ohos-cc.sh /tmp/ohos-cxx.sh`

### Step 1: 编译 OpenSSL 3.3.2

```bash
cd /tmp
tar xzf $PROJ_DIR/sources/openssl-3.3.2.tar.gz
cd openssl-3.3.2

./Configure linux-aarch64 \
  --prefix=$BUILD_DIR/openssl \
  --openssldir=$BUILD_DIR/openssl/ssl \
  no-shared no-tests no-ui-console \
  CC="/tmp/ohos-cc.sh" \
  AR="$OH_CLANG/llvm-ar" \
  RANLIB="$OH_CLANG/llvm-ranlib"

make -j$(nproc)
make install_sw
```

验证：`ls $BUILD_DIR/openssl/lib/libssl.a $BUILD_DIR/openssl/lib/libcrypto.a`

### Step 2: 编译 zlib 1.3.1

```bash
cd /tmp
tar xzf $PROJ_DIR/sources/zlib-1.3.1.tar.gz
cd zlib-1.3.1

CC=/tmp/ohos-cc.sh \
AR="$OH_CLANG/llvm-ar" \
RANLIB="$OH_CLANG/llvm-ranlib" \
./configure --prefix=$BUILD_DIR/zlib --static

make -j$(nproc)
make install
```

### Step 3: 编译 libffi 3.4.6

```bash
cd /tmp
tar xzf $PROJ_DIR/sources/libffi-3.4.6.tar.gz
cd libffi-3.4.6

./configure \
  --host=aarch64-unknown-linux-musl \
  --build=x86_64-linux-gnu \
  --prefix=$BUILD_DIR/libffi \
  --disable-shared --enable-static \
  CC=/tmp/ohos-cc.sh \
  AR="$OH_CLANG/llvm-ar" \
  RANLIB="$OH_CLANG/llvm-ranlib"

make -j$(nproc)
make install
```

### Step 4: 编译宿主机 Python（bootstrap）

交叉编译 CPython 需要一个同版本的宿主机 Python 来驱动构建。

如果已有 `build/python-host.tar.gz`，直接解压：

```bash
cd /tmp && tar xzf $BUILD_DIR/python-host.tar.gz
```

否则从源码编：

```bash
cd /tmp
tar xJf $PROJ_DIR/sources/Python-3.11.11.tar.xz
cd Python-3.11.11 && mkdir -p build-host && cd build-host
../configure --prefix=/tmp/python-host
make -j$(nproc)
make install
```

### Step 5: 编译 CPython 3.11.11 for OH

```bash
cd /tmp/Python-3.11.11

# 5a. 替换 configure（patch 了 triplet 检查）
cp $BUILD_DIR/configure.patched configure

# 5b. configure
mkdir -p build-ohos && cd build-ohos

ac_cv_file__dev_ptmx=no \
ac_cv_file__dev_ptc=no \
ac_cv_buggy_getaddrinfo=no \
ac_cv_have_long_long_format=yes \
../configure \
  --host=aarch64-unknown-linux-musl \
  --build=x86_64-linux-gnu \
  --prefix=$BUILD_DIR/python \
  --with-build-python=/tmp/python-host/bin/python3.11 \
  --with-openssl=$BUILD_DIR/openssl \
  --with-system-ffi \
  --disable-ipv6 \
  --without-ensurepip \
  CC=/tmp/ohos-cc.sh \
  CXX=/tmp/ohos-cxx.sh \
  AR="$OH_CLANG/llvm-ar" \
  RANLIB="$OH_CLANG/llvm-ranlib" \
  READELF="$OH_CLANG/llvm-readelf" \
  STRIP="$OH_CLANG/llvm-strip" \
  LDFLAGS="-L$BUILD_DIR/openssl/lib -L$BUILD_DIR/zlib/lib -L$BUILD_DIR/libffi/lib -static" \
  CFLAGS="-I$BUILD_DIR/openssl/include -I$BUILD_DIR/zlib/include -I$BUILD_DIR/libffi/include" \
  CPPFLAGS="-I$BUILD_DIR/openssl/include -I$BUILD_DIR/zlib/include -I$BUILD_DIR/libffi/include" \
  LIBFFI_CFLAGS="-I$BUILD_DIR/libffi/include" \
  LIBFFI_LIBS="-L$BUILD_DIR/libffi/lib -lffi"

# 5c. 替换 pyconfig.h（修复 OH musl 缺失的函数）
cp $BUILD_DIR/pyconfig.h.patched pyconfig.h

# 5d. 复制 Setup.local（31 个内建模块）
cp $BUILD_DIR/Setup.local Modules/Setup.local

# 5e. 编译
make -j$(nproc)

# 5f. 安装标准库
make install
```

### Step 6: Strip + 部署

```bash
# strip（17MB → 11MB）
$OH_CLANG/llvm-strip build-ohos/python -o $BUILD_DIR/python3-ohos

# 推到板子
hdc file send $BUILD_DIR/python3-ohos /data/local/tmp/python3
hdc shell 'chmod +x /data/local/tmp/python3'
```

## pyconfig.h Patch 说明

以下 5 个宏需要 `#undef`，因为 OH 的 musl 没有对应实现：

| 宏 | 原因 |
|---|---|
| `HAVE_PREADV2` | OH musl 没有 `preadv2` |
| `HAVE_PWRITEV2` | OH musl 没有 `pwritev2` |
| `HAVE_LIBINTL_H` | OH musl 没有 `libintl`（gettext） |
| `HAVE_LINUX_CAN_RAW_FD_FRAMES` | OH 内核头文件没有 SocketCAN 宏 |
| `HAVE_LINUX_CAN_RAW_JOIN_FILTERS` | 同上 |

如果 `pyconfig.h.patched` 因版本差异不可用，手动 patch：

```bash
sed -i 's/^#define HAVE_PREADV2 1/#undef HAVE_PREADV2/' pyconfig.h
sed -i 's/^#define HAVE_PWRITEV2 1/#undef HAVE_PWRITEV2/' pyconfig.h
sed -i 's/^#define HAVE_LIBINTL_H 1/#undef HAVE_LIBINTL_H/' pyconfig.h
sed -i 's/^#define HAVE_LINUX_CAN_RAW_FD_FRAMES 1/#undef HAVE_LINUX_CAN_RAW_FD_FRAMES/' pyconfig.h
sed -i 's/^#define HAVE_LINUX_CAN_RAW_JOIN_FILTERS 1/#undef HAVE_LINUX_CAN_RAW_JOIN_FILTERS/' pyconfig.h
```

## Setup.local 内建模块列表

静态链接的 Python 不支持 `dlopen`，所有关键模块必须编为内建：

```
zlib _ssl _hashlib _socket binascii select _struct _json
_datetime math _random _sha256 _sha512 _sha1 _sha3 _md5
_pickle _asyncio _bisect _heapq _csv _posixsubprocess
_contextvars _typing _queue array fcntl mmap _opcode
_statistics unicodedata
```

未编入的模块（OH musl 缺失函数或不需要）：
- `_blake2`：`explicit_bzero` 缺失，hashlib 有 sha256 fallback
- `_ctypes`：多文件内建编译复杂，后续按需加入
- `nis` / `spwd`：古老的 NIS/shadow 密码，不需要
- `_sqlite3`：被 configure 禁用，后续按需加入

## configure.patched 说明

CPython 的 `configure` 脚本有一处 triplet 一致性检查：编译器 `--print-multiarch` 报的是 `aarch64-linux-ohos`，但 `--host` 传的是 `aarch64-unknown-linux-musl`，两者不一致会报错退出。

Patch 内容（将报错改为采用 `PLATFORM_TRIPLET`）：

```
原：as_fn_error $? "internal configure error for the platform triplet, please file a bug report"
改：MULTIARCH=$PLATFORM_TRIPLET  # patched for OHOS
```

## 已验证（RK3568 板子）

```
$ PYTHONHOME=/data/local/tmp/python-home /data/local/tmp/python3 test.py
Python 3.11.11 (main, Mar 10 2026, 20:22:12) [Clang 15.0.4]
zlib OK
ssl: OpenSSL 3.3.2 3 Sep 2024
socket OK
json OK
```
