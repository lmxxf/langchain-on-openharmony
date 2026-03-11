#!/bin/bash
# Build OpenSSL 3.3.2 as shared library for OHOS

PROJDIR=/mnt/c/Users/lmxxf/openclaw_on_openharmony/LangChain
SRCDIR=$PROJDIR/sources/openssl-3.3.2
INSTALLDIR=$PROJDIR/build/openssl-shared

CC=/tmp/ohos-cc.sh
AR=/home/lmxxf/oh6/source/prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-ar
RANLIB=/home/lmxxf/oh6/source/prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-ranlib

# Clean previous build
cd $SRCDIR
make clean 2>/dev/null

./Configure linux-aarch64 \
  --prefix=$INSTALLDIR \
  --cross-compile-prefix= \
  shared \
  no-tests \
  no-ui-console \
  -Wl,--dynamic-linker,/system/lib/ld-musl-aarch64.so.1 \
  CC=$CC \
  AR=$AR \
  RANLIB=$RANLIB

make -j$(nproc)
make install_sw
