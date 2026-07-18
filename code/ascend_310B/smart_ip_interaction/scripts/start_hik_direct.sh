#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

if [[ -f /usr/local/Ascend/ascend-toolkit/set_env.sh ]]; then
  # shellcheck disable=SC1091
  source /usr/local/Ascend/ascend-toolkit/set_env.sh
elif [[ -f /usr/local/Ascend/ascend-toolkit/latest/bin/setenv.bash ]]; then
  # shellcheck disable=SC1091
  source /usr/local/Ascend/ascend-toolkit/latest/bin/setenv.bash
fi

export MVCAM_SDK_PATH="${MVCAM_SDK_PATH:-/opt/MVS}"
export MVCAM_COMMON_RUNENV="${MVCAM_COMMON_RUNENV:-/opt/MVS/lib}"
export MVCAM_GENICAM_CLPROTOCOL="${MVCAM_GENICAM_CLPROTOCOL:-/opt/MVS/lib/CLProtocol}"
export ALLUSERSPROFILE="${ALLUSERSPROFILE:-/opt/MVS/MVFG}"
export LD_LIBRARY_PATH="/opt/MVS/lib/aarch64:/opt/MVS/lib:${LD_LIBRARY_PATH:-}"
set -u

export SMART_IP_USE_UDP="${SMART_IP_USE_UDP:-0}"
export SMART_IP_VIDEO_BACKEND="${SMART_IP_VIDEO_BACKEND:-hik_gige}"
export SMART_IP_HIK_CAMERA_IP="${SMART_IP_HIK_CAMERA_IP:-192.168.1.200}"
export SMART_IP_HIK_SERIAL_NUMBER="${SMART_IP_HIK_SERIAL_NUMBER:-DA9807825}"
export SMART_IP_HIK_LATEST_FRAME="${SMART_IP_HIK_LATEST_FRAME:-1}"
export SMART_IP_HIK_ACQUISITION_FPS="${SMART_IP_HIK_ACQUISITION_FPS:-12}"
export SMART_IP_BACKEND_HOST="${SMART_IP_BACKEND_HOST:-192.168.1.1}"
export SMART_IP_BACKEND_PORT="${SMART_IP_BACKEND_PORT:-9000}"
export SMART_IP_STREAM_ENABLED="${SMART_IP_STREAM_ENABLED:-1}"
export SMART_IP_STREAM_PORT="${SMART_IP_STREAM_PORT:-8081}"
export SMART_IP_STREAM_MAX_FPS="${SMART_IP_STREAM_MAX_FPS:-6}"
export SMART_IP_STREAM_MAX_WIDTH="${SMART_IP_STREAM_MAX_WIDTH:-640}"
export SMART_IP_STREAM_JPEG_QUALITY="${SMART_IP_STREAM_JPEG_QUALITY:-65}"
export SMART_IP_TCP_QUEUE_SIZE="${SMART_IP_TCP_QUEUE_SIZE:-30}"
export SMART_IP_TCP_MESSAGE_TTL="${SMART_IP_TCP_MESSAGE_TTL:-5}"
export SMART_IP_BENCH_DIR="${SMART_IP_BENCH_DIR:-$PROJECT_ROOT/log/hik_gige}"

exec python3 -u run_pipeline.py "$@"
