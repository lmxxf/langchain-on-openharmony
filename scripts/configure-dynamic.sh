#!/bin/bash
# Configure CPython 3.11.11 dynamic build for OpenHarmony 6.0

PROJDIR=/mnt/c/Users/lmxxf/openclaw_on_openharmony/LangChain
SRCDIR=$PROJDIR/sources/Python-3.11.11
BUILDDIR=$SRCDIR/build-ohos-dynamic
OPENSSL=$PROJDIR/build/openssl-shared
ZLIB=$PROJDIR/build/zlib-pic
LIBFFI=$PROJDIR/build/libffi

cd $BUILDDIR

$SRCDIR/configure \
  --host=aarch64-unknown-linux-ohos \
  --build=x86_64-linux-gnu \
  --with-build-python=/tmp/python-host/bin/python3.11 \
  --enable-shared \
  --prefix=/data/local/tmp/python-home \
  --with-openssl=$OPENSSL \
  --with-openssl-rpath=no \
  CC=/tmp/ohos-cc.sh \
  CXX=/tmp/ohos-cxx.sh \
  AR=/home/lmxxf/oh6/source/prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-ar \
  READELF=/home/lmxxf/oh6/source/prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-readelf \
  CFLAGS="-I$OPENSSL/include -I$ZLIB/include -I$LIBFFI/include" \
  LDFLAGS="-L$OPENSSL/lib -L$ZLIB/lib -L$LIBFFI/lib -Wl,-rpath,/data/local/tmp/lib -Wl,--dynamic-linker,/system/lib/ld-musl-aarch64.so.1" \
  LIBS="-lssl -lcrypto -lz -lffi" \
  ac_cv_buggy_getaddrinfo=no \
  ac_cv_file__dev_ptmx=yes \
  ac_cv_file__dev_ptc=no
