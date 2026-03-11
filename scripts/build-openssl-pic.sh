#!/bin/bash
# Build OpenSSL 3.3.2 with -fPIC for dynamic CPython on OHOS

PROJDIR=/mnt/c/Users/lmxxf/openclaw_on_openharmony/LangChain
SRCDIR=$PROJDIR/sources/openssl-3.3.2
INSTALLDIR=$PROJDIR/build/openssl-pic

CC=/tmp/ohos-cc.sh
AR=/home/lmxxf/oh6/source/prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-ar
RANLIB=/home/lmxxf/oh6/source/prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-ranlib

cd $SRCDIR

./Configure linux-aarch64 \
  --prefix=$INSTALLDIR \
  --cross-compile-prefix= \
  no-shared \
  no-tests \
  no-ui-console \
  -fPIC \
  CC=$CC \
  AR=$AR \
  RANLIB=$RANLIB

make -j$(nproc)
make install_sw
