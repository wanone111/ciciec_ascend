#!/bin/bash
#===============================================================================
# 文件名: set_env.sh
# 功能: 配置 Ascend CANN + 交叉编译工具链环境
# 用法: source /path/to/set_env.sh    # ⚠️ 必须用 source 执行
#===============================================================================

#-------------------------------------------------------------------------------
# 🛠️ 核心路径配置（已按您提供的路径精确对齐）
#-------------------------------------------------------------------------------
ASCEND_TOOLKIT_ROOT="/home/bb/Ascend/ascend-toolkit"
CANN_VERSION="latest"  # 如需切换具体版本，改为如 "8.0.0" 即可全局生效
TOOLCHAIN_BIN="/home/bb/competition_ws/toolchain/toolchain/bin"

# ASCEND_HOME 动态指向目标架构目录
export ASCEND_HOME="${ASCEND_TOOLKIT_ROOT}/${CANN_VERSION}/aarch64-linux"

#-------------------------------------------------------------------------------
# 🛡️ 防重复加载机制
#-------------------------------------------------------------------------------
if [ -n "${ASCEND_ENV_LOADED}" ]; then
    echo "[INFO] Ascend environment already loaded, skipping..."
    return 0 2>/dev/null || exit 0
fi

#-------------------------------------------------------------------------------
# 1. 注入 CANN 官方环境变量
#-------------------------------------------------------------------------------
if [ -f "${ASCEND_TOOLKIT_ROOT}/set_env.sh" ]; then
    echo "[INFO] Loading official CANN environment from ${ASCEND_TOOLKIT_ROOT}..."
    source "${ASCEND_TOOLKIT_ROOT}/set_env.sh" || {
        echo "[ERROR] Failed to source CANN set_env.sh"
        return 1 2>/dev/null || exit 1
    }
else
    echo "[ERROR] CANN set_env.sh not found: ${ASCEND_TOOLKIT_ROOT}/set_env.sh"
    return 1 2>/dev/null || exit 1
fi

#-------------------------------------------------------------------------------
# 2. 补全深层驱动模拟库路径（解决 libascend_hal.so 等依赖缺失）
#-------------------------------------------------------------------------------
CANN_DEVLIB_PATH="${ASCEND_TOOLKIT_ROOT}/${CANN_VERSION}/x86_64-linux/devlib/linux/x86_64"

# 安全追加 PATH
if [[ ":$PATH:" != *":${ASCEND_HOME}/atc/bin:"* ]]; then
    export PATH="${ASCEND_HOME}/atc/bin:${ASCEND_HOME}/bin:${PATH}"
fi

# 安全追加 LD_LIBRARY_PATH（防覆盖系统库、防重复）
if [[ ":$LD_LIBRARY_PATH:" != *":${CANN_DEVLIB_PATH}:"* ]]; then
    export LD_LIBRARY_PATH="${ASCEND_HOME}/lib64:${ASCEND_HOME}/atc/lib64:${CANN_DEVLIB_PATH}:${LD_LIBRARY_PATH}"
fi

echo "[INFO] ASCEND_HOME set to: ${ASCEND_HOME}"
echo "[INFO] LD_LIBRARY_PATH appended with devlib & runtime libs"

#-------------------------------------------------------------------------------
# 3. 限制 TE 编译器并发数（防内存打满 OOM）
#-------------------------------------------------------------------------------
export TE_PARALLEL_COMPILER=2
echo "[INFO] TE_PARALLEL_COMPILER=2 (OOM-safe)"

#-------------------------------------------------------------------------------
# 4. 配置交叉编译工具链与专用变量
#-------------------------------------------------------------------------------
if [ -d "${TOOLCHAIN_BIN}" ]; then
    if [[ ":$PATH:" != *":${TOOLCHAIN_BIN}:"* ]]; then
        export PATH="${TOOLCHAIN_BIN}:${PATH}"
        echo "[INFO] Added cross-toolchain to PATH"
    fi
fi

export CROSS_COMPILE=aarch64-linux-gnu-
export CC=${CROSS_COMPILE}gcc
export CXX=${CROSS_COMPILE}g++
export AR=${CROSS_COMPILE}ar
export LD=${CROSS_COMPILE}ld
export STRIP=${CROSS_COMPILE}strip
export OBJCOPY=${CROSS_COMPILE}objcopy
export OBJDUMP=${CROSS_COMPILE}objdump

# CANN 开发路径（依赖官方 set_env.sh 导出的 ASCEND_TOOLKIT_PATH）
if [ -n "${ASCEND_TOOLKIT_PATH}" ]; then
    export ASCEND_AARCH64_LIB="${ASCEND_TOOLKIT_PATH}/runtime/lib64/stub/aarch64"
    export ASCEND_INC_PATH="${ASCEND_TOOLKIT_PATH}/include"
fi

#-------------------------------------------------------------------------------
# 5. 标记已加载 + 打印确认信息
#-------------------------------------------------------------------------------
export ASCEND_ENV_LOADED=1

echo ""
echo "✅ Ascend Cross-Compile Environment Loaded Successfully!"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  ASCEND_HOME      : ${ASCEND_HOME}"
echo "  CXX              : ${CXX}"
echo "  TE_PARALLEL      : ${TE_PARALLEL_COMPILER}"
echo "  LIB_PATH         : ${ASCEND_HOME}/lib64 + devlib"
if [ -n "${ASCEND_INC_PATH}" ]; then
    echo "  INC_PATH         : ${ASCEND_INC_PATH}"
fi
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "💡 验证: aarch64-linux-gnu-g++ --version"
echo "💡 检查: check_ascend_env"
echo ""

#-------------------------------------------------------------------------------
# 6. 快速环境验证函数
#-------------------------------------------------------------------------------
function check_ascend_env() {
    echo "🔍 Checking environment..."
    command -v aarch64-linux-gnu-g++ >/dev/null 2>&1 && \
        echo "  ✅ Cross-compiler: $(aarch64-linux-gnu-g++ --version | head -1)" || \
        echo "  ❌ Cross-compiler: NOT FOUND"
    
    [ -n "${ASCEND_INC_PATH}" ] && [ -f "${ASCEND_INC_PATH}/acl/acl.h" ] && \
        echo "  ✅ CANN header: acl.h found" || \
        echo "  ❌ CANN header: NOT FOUND"
    
    [ -n "${ASCEND_AARCH64_LIB}" ] && [ -f "${ASCEND_AARCH64_LIB}/libascendcl.so" ] && \
        echo "  ✅ CANN stub lib: libascendcl.so found" || \
        echo "  ❌ CANN stub lib: NOT FOUND"
        
    echo "  📦 TE Parallel Limit: ${TE_PARALLEL_COMPILER}"
}