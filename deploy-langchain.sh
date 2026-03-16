#!/bin/bash
# LangChain on OpenHarmony 一键部署脚本
# 在 WSL 里跑，通过 hdc 推文件到板子
#
# 用法: ./deploy-langchain.sh

set -e

# ============================================================
# 路径配置
# ============================================================
LANGCHAIN_DIR="/mnt/c/Users/lmxxf/openclaw_on_openharmony/LangChain"
DEPLOY_SRC="$LANGCHAIN_DIR/build/python-dynamic/data/local/tmp"
DEPLOY_PKG="$LANGCHAIN_DIR/build/langchain-ohos-deploy.tar.gz"
CACERT="$LANGCHAIN_DIR/build/cacert.pem"
DYNLOAD_DIR="$DEPLOY_SRC/python-home/lib/python3.11/lib-dynload"

BOARD_BASE="/data/local/tmp"

# 需要 patchelf 修复的 .so 模块
PATCH_MODULES=(_hashlib _sha1 _sha256 _sha3 _sha512 _ssl)

# ============================================================
# 工具函数
# ============================================================
hdc_shell() {
    powershell.exe -Command "hdc shell '$1'" 2>&1
}

hdc_send() {
    local win_src
    win_src="$(wslpath -w "$1")"
    powershell.exe -Command "hdc file send '$win_src' '$2'" 2>&1
}

info()  { echo -e "\033[32m[+]\033[0m $1"; }
warn()  { echo -e "\033[33m[!]\033[0m $1"; }
fail()  { echo -e "\033[31m[x]\033[0m $1"; exit 1; }

# ============================================================
# Step 1: 检查 hdc 连接
# ============================================================
info "检查 hdc 连接..."
UNAME=$(hdc_shell 'uname -a' | tr -d '\r')
if [[ "$UNAME" != *aarch64* ]]; then
    fail "hdc 未连接或设备不是 aarch64: $UNAME"
fi
info "设备: $UNAME"

# ============================================================
# Step 2: 检查源文件
# ============================================================
info "检查编译产物..."
[ -d "$DEPLOY_SRC" ] || fail "编译产物目录不存在: $DEPLOY_SRC"

if [ ! -f "$CACERT" ]; then
    warn "CA 证书不存在，正在下载..."
    wget -q -O "$CACERT" https://curl.se/ca/cacert.pem || fail "下载 CA 证书失败"
fi
info "CA 证书: OK"

# ============================================================
# Step 3: patchelf 修复
# ============================================================
info "检查 .so 模块 libpython 依赖..."

if ! command -v patchelf &>/dev/null; then
    warn "patchelf 未安装，正在安装..."
    sudo apt-get install -y patchelf >/dev/null 2>&1 || fail "安装 patchelf 失败"
fi

for mod in "${PATCH_MODULES[@]}"; do
    SO_FILE="$DYNLOAD_DIR/${mod}.cpython-311-aarch64-linux-ohos.so"
    [ -f "$SO_FILE" ] || continue
    if ! readelf -d "$SO_FILE" 2>/dev/null | grep -q "libpython3.11.so"; then
        patchelf --add-needed libpython3.11.so "$SO_FILE"
        info "  patched: $mod"
    fi
done
info "patchelf 检查完成"

# ============================================================
# Step 4: 打包
# ============================================================
info "打包部署包..."
(cd "$DEPLOY_SRC" && tar czf "$DEPLOY_PKG" \
    --exclude='*/test' \
    --exclude='*/tests' \
    --exclude='*/idlelib' \
    --exclude='*/tkinter' \
    --exclude='*/turtledemo' \
    --exclude='*/lib2to3' \
    --exclude='*/ensurepip' \
    --exclude='*/__pycache__' \
    --exclude='*/_test*.so' \
    --exclude='*/xx*.so' \
    *)
info "部署包: $(du -h "$DEPLOY_PKG" | cut -f1)"

# ============================================================
# Step 5: 推送 + 解压
# ============================================================
info "推送部署包到板子..."
hdc_send "$DEPLOY_PKG" "$BOARD_BASE/langchain-ohos-deploy.tar.gz"

info "解压..."
hdc_shell "cd $BOARD_BASE && tar xzf langchain-ohos-deploy.tar.gz && rm langchain-ohos-deploy.tar.gz"

info "推送 CA 证书..."
hdc_send "$CACERT" "$BOARD_BASE/cacert.pem"

# ============================================================
# Step 6: 设置系统时间
# ============================================================
BOARD_YEAR=$(hdc_shell 'date +%Y' | tr -d '\r')
if [ "$BOARD_YEAR" != "$(date +%Y)" ]; then
    hdc_shell "date $(date '+%m%d%H%M%Y.%S')" >/dev/null
    info "板子时间已校准"
else
    info "板子时间正常"
fi

# ============================================================
# Step 7: 验证
# ============================================================
info "运行验证测试..."

TEST_SCRIPT=$(mktemp /tmp/deploy_test_XXXXXX.py)
cat > "$TEST_SCRIPT" << 'PYEOF'
import os, sys

os.write(1, f"  Python: {sys.version.split()[0]}\n".encode())

for mod in ['json', 'ssl', 'socket', 'hashlib']:
    try:
        m = __import__(mod)
        if mod == 'ssl':
            os.write(1, f"  {mod}: {m.OPENSSL_VERSION}\n".encode())
        else:
            os.write(1, f"  {mod}: OK\n".encode())
    except Exception as e:
        os.write(1, f"  {mod}: FAIL - {e}\n".encode())
        sys.exit(1)

try:
    import pydantic_core
    os.write(1, f"  pydantic_core: {pydantic_core.__version__}\n".encode())
except Exception as e:
    os.write(1, f"  pydantic_core: FAIL - {e}\n".encode())
    sys.exit(1)

try:
    import langchain_core
    os.write(1, b"  langchain_core: OK\n")
except Exception as e:
    os.write(1, f"  langchain_core: FAIL - {e}\n".encode())
    sys.exit(1)

os.write(1, b"\n=== All imports OK ===\n")
PYEOF

hdc_send "$TEST_SCRIPT" "$BOARD_BASE/_deploy_test.py"
rm -f "$TEST_SCRIPT"

PY_ENV="LD_LIBRARY_PATH=$BOARD_BASE/lib PYTHONHOME=$BOARD_BASE/python-home SSL_CERT_FILE=$BOARD_BASE/cacert.pem"
RESULT=$(hdc_shell "$PY_ENV $BOARD_BASE/python-home/bin/python3.11 $BOARD_BASE/_deploy_test.py")
echo "$RESULT"

hdc_shell "rm -f $BOARD_BASE/_deploy_test.py"

if echo "$RESULT" | grep -q "All imports OK"; then
    echo ""
    info "========================================="
    info "  LangChain on OpenHarmony 部署成功!"
    info "========================================="
    echo ""
    info "运行命令:"
    echo "  hdc shell 'LD_LIBRARY_PATH=/data/local/tmp/lib \\"
    echo "    PYTHONHOME=/data/local/tmp/python-home \\"
    echo "    SSL_CERT_FILE=/data/local/tmp/cacert.pem \\"
    echo "    /data/local/tmp/python-home/bin/python3.11 your_script.py'"
else
    fail "验证失败！请检查上方输出。"
fi
