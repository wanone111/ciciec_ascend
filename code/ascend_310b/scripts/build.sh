#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

if [[ -f /home/bb/competition_ws/ciciec_ascend/Scripts/ascend_310b/set_env.sh ]]; then
    # shellcheck disable=SC1091
    set +u
    source /home/bb/competition_ws/ciciec_ascend/Scripts/ascend_310b/set_env.sh
    set -u
fi

cmake -S "${PROJECT_DIR}" -B "${PROJECT_DIR}/build" \
    -DCMAKE_TOOLCHAIN_FILE="${PROJECT_DIR}/toolchain/aarch64.cmake" \
    -DASCEND_HOME="${ASCEND_HOME:-/home/bb/Ascend/ascend-toolkit/latest}" \
    -DASCEND_INC_PATH="${ASCEND_INC_PATH:-${ASCEND_HOME:-/home/bb/Ascend/ascend-toolkit/latest}/../include}" \
    -DASCEND_AARCH64_LIB="${ASCEND_AARCH64_LIB:-${ASCEND_HOME:-/home/bb/Ascend/ascend-toolkit/latest}/../runtime/lib64/stub/aarch64}"

cmake --build "${PROJECT_DIR}/build" -j"$(nproc)"
