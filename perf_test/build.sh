#!/bin/bash
# 交叉编译 fb_perf.c for OpenHarmony aarch64
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_ROOT="/home/lmxxf/oh6/source"
CLANG="$SOURCE_ROOT/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang"
SYSROOT="$SOURCE_ROOT/prebuilts/ohos-sdk/linux/20/native/sysroot"

echo "Compiling fb_perf..."
$CLANG --target=aarch64-unknown-linux-ohos --sysroot="$SYSROOT" \
    -O2 -o "$SCRIPT_DIR/fb_perf" "$SCRIPT_DIR/fb_perf.c"

echo "Done: $SCRIPT_DIR/fb_perf ($(du -h "$SCRIPT_DIR/fb_perf" | cut -f1))"

echo ""
echo "部署命令："
echo "  powershell.exe -Command \"hdc file send '$(wslpath -w "$SCRIPT_DIR/fb_perf")' '/data/local/tmp/fb_perf'\""
echo "  powershell.exe -Command \"hdc shell 'chmod 755 /data/local/tmp/fb_perf && /data/local/tmp/fb_perf'\""
echo ""
echo "查看日志："
echo "  powershell.exe -Command \"hdc shell 'cat /data/local/tmp/fb_perf.log'\""
