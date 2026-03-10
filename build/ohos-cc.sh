#!/bin/bash
exec /home/lmxxf/oh6/source/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang \
  --target=aarch64-unknown-linux-ohos \
  --sysroot=/home/lmxxf/oh6/source/prebuilts/ohos-sdk/linux/20/native/sysroot \
  "$@"
