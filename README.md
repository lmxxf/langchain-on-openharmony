# LangChain on OpenHarmony 6.0 (RK3568, aarch64)

在 OpenHarmony 开发板上跑 Python 3.11 + LangChain + DeepSeek API。已验证全链路打通。

```
OH RK3568 → CPython 3.11.11 → pydantic 2.12.5 → langchain-core 1.2.18
    → openai SDK → HTTPS/TLS → DeepSeek API → AIMessage
```

## 快速部署（已有编译产物）

适用场景：clone 下来，3 条命令把 LangChain 跑在 OH 开发板上。

### 前提

- 宿主机能通过 `hdc` 连接到 RK3568 开发板
- 板子上有 `/data/local/tmp/`（14G+ 可用空间）

### 步骤

**1. 解压部署包**

`build/langchain-ohos-deploy.tar.gz`（25MB）包含全部编译产物：CPython 3.11 + OpenSSL + LangChain 全依赖链。

```bash
cd LangChain/
mkdir -p deploy
tar xzf build/langchain-ohos-deploy.tar.gz -C deploy/

# CA 证书（HTTPS 必须）
wget -q https://curl.se/ca/cacert.pem -O deploy/cacert.pem
```

**2. 推到板子**

```bash
hdc file send build/langchain-ohos-deploy.tar.gz /data/local/tmp/langchain-ohos-deploy.tar.gz
hdc shell "cd /data/local/tmp && tar xzf langchain-ohos-deploy.tar.gz && rm langchain-ohos-deploy.tar.gz"

# CA 证书
hdc file send deploy/cacert.pem /data/local/tmp/cacert.pem
```

**3. 验证**

```bash
hdc shell '\
  LD_LIBRARY_PATH=/data/local/tmp/lib \
  PYTHONHOME=/data/local/tmp/python-home \
  SSL_CERT_FILE=/data/local/tmp/cacert.pem \
  /data/local/tmp/python-home/bin/python3.11 -c "
import ssl; print(\"ssl:\", ssl.OPENSSL_VERSION)
from pydantic_core import __version__; print(\"pydantic-core:\", __version__)
from langchain_core.messages import AIMessage; print(\"langchain-core: OK\")
print(\"ALL OK\")
"'
```

**4. 跑 LangChain**

把你的 Python 脚本推上去，用这个命令模板运行：

```bash
LD_LIBRARY_PATH=/data/local/tmp/lib \
PYTHONHOME=/data/local/tmp/python-home \
SSL_CERT_FILE=/data/local/tmp/cacert.pem \
/data/local/tmp/python-home/bin/python3.11 your_script.py
```

示例脚本（调 DeepSeek API）：

```python
from langchain_core.messages import HumanMessage, AIMessage
from openai import OpenAI

client = OpenAI(api_key="你的key", base_url="https://api.deepseek.com/v1")
messages = [HumanMessage(content="Say hello in one sentence.")]
response = client.chat.completions.create(
    model="deepseek-chat",
    messages=[{"role": "user", "content": m.content} for m in messages],
)
print(AIMessage(content=response.choices[0].message.content))
```

### 板子上的最终目录结构

```
/data/local/tmp/
├── python-home/
│   ├── bin/python3.11              # CPython 解释器
│   └── lib/python3.11/
│       ├── site-packages/          # LangChain 全家桶
│       │   ├── pydantic_core/      # Rust .so (4.4MB)
│       │   ├── pydantic/           # 纯 Python
│       │   ├── langchain_core/     # 纯 Python
│       │   ├── openai/             # 纯 Python
│       │   ├── jiter/              # Rust .so
│       │   ├── uuid_utils/         # Rust .so
│       │   ├── xxhash/             # C .so
│       │   └── ...（20+ 纯 Python 包）
│       ├── lib-dynload/            # 65 个 CPython .so 扩展
│       └── ...（标准库）
├── lib/
│   ├── libpython3.11.so.1.0       # 4.8MB
│   ├── libcrypto.so.3              # 4.2MB
│   ├── libssl.so.3                 # 836KB
│   └── 软链接
└── cacert.pem                      # Mozilla CA 证书
```

---

## 从头编译

适用场景：没有编译产物，或想改 Python 版本 / 依赖。

### 前提

- OpenHarmony 源码树（需要其中的 Clang 15 工具链和 musl sysroot）
- Linux 宿主机（WSL2 / Ubuntu）
- Rust 1.94+（`rustup target add aarch64-unknown-linux-ohos`）
- maturin（`pip3 install maturin`）
- wget、tar、make、perl

### 环境变量

```bash
# 按你的实际路径改
OH_SRC=/home/lmxxf/oh6/source
OH_CLANG=$OH_SRC/prebuilts/clang/ohos/linux-x86_64/llvm/bin
OH_SYSROOT=$OH_SRC/prebuilts/ohos-sdk/linux/20/native/sysroot
PROJ=$(pwd)  # LangChain/ 目录
```

### Step 0: 编译器 Wrapper

CPython 的 configure 不接受 CC 带参数，用 wrapper 脚本绕过：

```bash
cat > /tmp/ohos-cc.sh << EOF
#!/bin/bash
exec $OH_CLANG/clang --target=aarch64-unknown-linux-ohos --sysroot=$OH_SYSROOT "\$@"
EOF
cat > /tmp/ohos-cxx.sh << EOF
#!/bin/bash
exec $OH_CLANG/clang++ --target=aarch64-unknown-linux-ohos --sysroot=$OH_SYSROOT "\$@"
EOF
chmod +x /tmp/ohos-cc.sh /tmp/ohos-cxx.sh
```

### Step 1: OpenSSL 3.3.2

**必须编 shared（.so）**，不能编 static（.a）。static .a 链进 Python 的 .so 扩展时会 segfault。

```bash
cd sources/openssl-3.3.2
./Configure linux-aarch64 --prefix=$PROJ/build/openssl-shared \
  shared no-tests no-ui-console \
  -Wl,--dynamic-linker,/system/lib/ld-musl-aarch64.so.1 \
  CC=/tmp/ohos-cc.sh AR=$OH_CLANG/llvm-ar RANLIB=$OH_CLANG/llvm-ranlib
make -j$(nproc) && make install_sw
```

### Step 2: zlib 1.3.1

```bash
cd sources/zlib-1.3.1
CC=/tmp/ohos-cc.sh CFLAGS="-fPIC" ./configure --prefix=$PROJ/build/zlib-pic --static
make -j$(nproc) && make install
```

### Step 3: 宿主机 Python（bootstrap）

交叉编译 CPython 需要同版本的宿主机 Python。如果 `build/python-host.tar.gz` 还在，解压到 `/tmp/python-host/` 即可。否则：

```bash
cd /tmp
wget https://www.python.org/ftp/python/3.11.11/Python-3.11.11.tgz
tar xzf Python-3.11.11.tgz && cd Python-3.11.11
./configure --prefix=/tmp/python-host && make -j$(nproc) && make install
```

### Step 4: CPython 3.11.11（动态链接）

```bash
cd sources/Python-3.11.11

# 应用 OH 官方 patch（修 config.sub 识别 ohos）
patch -p1 < $OH_SRC/third_party/python/patches/cross_compile_support_ohos.patch

# 手动 patch setup.py：DISABLED_MODULE_LIST 改为只禁 ['_uuid']
# 手动 patch pyconfig.h：#undef HAVE_LIBINTL_H, HAVE_LINUX_CAN_RAW_*

mkdir -p build-ohos-dynamic && cd build-ohos-dynamic
../configure \
  --host=aarch64-unknown-linux-ohos \
  --build=x86_64-linux-gnu \
  --with-build-python=/tmp/python-host/bin/python3.11 \
  --enable-shared \
  --prefix=/data/local/tmp/python-home \
  --with-openssl=$PROJ/build/openssl-shared \
  --with-openssl-rpath=no \
  CC=/tmp/ohos-cc.sh CXX=/tmp/ohos-cxx.sh \
  AR=$OH_CLANG/llvm-ar READELF=$OH_CLANG/llvm-readelf \
  CFLAGS="-I$PROJ/build/openssl-shared/include -I$PROJ/build/zlib-pic/include" \
  LDFLAGS="-L$PROJ/build/openssl-shared/lib -L$PROJ/build/zlib-pic/lib \
    -Wl,-rpath,/data/local/tmp/lib \
    -Wl,--dynamic-linker,/system/lib/ld-musl-aarch64.so.1" \
  LIBS="-lssl -lcrypto -lz" \
  ac_cv_buggy_getaddrinfo=no ac_cv_file__dev_ptmx=yes ac_cv_file__dev_ptc=no
```

**关键：patch Makefile 的 BLDSHARED**

```makefile
BLDSHARED = $(CC) -shared $(PY_CORE_LDFLAGS) -L. -lpython3.11
```

不加这行，一半 .so 模块会 segfault。原因：OHOS musl 的 dlopen 不会让新加载的 .so 看到调用者进程的符号，每个 .so 必须显式声明依赖 libpython。

```bash
make -j$(nproc)
make install DESTDIR=$PROJ/build/python-dynamic
```

> setup.py 会报很多模块 "Failed to build"——这是**假报错**。交叉编译时 setup.py 会尝试在宿主机上 import 刚编好的 aarch64 .so，当然失败。实际 .so 文件已经编好了，忽略即可。

### Step 5: Rust 扩展（pydantic-core, uuid-utils, jiter）

```bash
# 一次性配置 cargo
cat >> ~/.cargo/config.toml << 'EOF'
[target.aarch64-unknown-linux-ohos]
linker = "/tmp/ohos-cc.sh"
EOF

rustup target add aarch64-unknown-linux-ohos
```

三个包用同一个模板（以 pydantic-core 为例）：

```bash
cd sources/pydantic-core

RUSTFLAGS="-L $PROJ/build/python-dynamic/data/local/tmp/python-home/lib -l python3.11" \
maturin build \
  --target aarch64-unknown-linux-ohos \
  --release \
  --interpreter python3.11 \
  --skip-auditwheel

# 产物在 target/wheels/*.whl，解压后丢进 site-packages
```

`RUSTFLAGS` 加 `-l python3.11` 是同一个坑——OHOS musl dlopen 不继承符号。

uuid-utils 和 jiter 完全同理，换个目录跑同样的命令。

### Step 6: xxhash（C 扩展）

```bash
/tmp/ohos-cc.sh -shared -fPIC -O2 \
  -I$PROJ/build/python-dynamic/data/local/tmp/python-home/include/python3.11 \
  -I sources/xxhash/deps/xxhash \
  -L$PROJ/build/python-dynamic/data/local/tmp/python-home/lib -lpython3.11 \
  -o _xxhash.cpython-311-aarch64-linux-ohos.so \
  sources/xxhash/src/_xxhash.c sources/xxhash/deps/xxhash/xxhash.c
```

放到 `site-packages/xxhash/` 目录里（不是 site-packages 根目录）。

### Step 7: 纯 Python 包

在宿主机上 pip download，解压丢进 site-packages：

```bash
pip3 download pydantic langchain-core langsmith openai httpx httpcore \
  requests anyio certifi idna h11 sniffio jsonpatch jsonpointer \
  packaging tenacity typing-extensions typing-inspection annotated-types \
  distro tqdm urllib3 requests-toolbelt \
  --only-binary=:all: --no-deps -d wheels/

for whl in wheels/*.whl; do unzip -o "$whl" -d site-packages/; done
```

然后按"快速部署"的步骤推上板子。

---

## 踩坑备忘

详细排查过程见 [DevHistory.md](DevHistory.md)。

| 坑 | 根因 | 修法 |
|:---|:---|:---|
| .so 扩展模块 segfault | OHOS musl dlopen 不继承调用者进程符号 | 所有 .so 必须 `-lpython3.11` |
| OpenSSL static .a → segfault | .a 链进 .so 时 ABI 问题 | OpenSSL 必须编 shared |
| setup.py 报 "Failed to build" | 交叉编译时在宿主机 import aarch64 .so | 假报错，忽略 |
| pip install langchain 装到 0.0.x | PyPI 没有 ohos wheel | 自己编 pydantic-core 等扩展 |
| `ld-musl-namespace` 隔离 | `/data/local/tmp` 走默认命名空间 | 不是障碍，LD_LIBRARY_PATH 直接能用 |
| OH 官方 CPython patch 漏了 Makefile | patch 只改了 setup.py 加 -lpython3.11 | 手动 patch BLDSHARED |

## 版本信息

| 组件 | 版本 |
|:---|:---|
| OpenHarmony | 6.0 |
| 开发板 | RK3568 (aarch64) |
| CPython | 3.11.11 |
| OpenSSL | 3.3.2 |
| zlib | 1.3.1 |
| pydantic | 2.12.5 |
| pydantic-core | 2.41.5 |
| langchain-core | 1.2.18 |
| openai SDK | 最新 |
| Rust | 1.94.0 |
| maturin | 1.12.6 |
