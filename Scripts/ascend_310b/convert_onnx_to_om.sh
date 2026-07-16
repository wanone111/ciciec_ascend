#!/usr/bin/env bash
# Convert project ONNX models to Ascend OM with ATC.
#
# Usage:
#   bash Scripts/ascend_310b/convert_onnx_to_om.sh emotion
#   bash Scripts/ascend_310b/convert_onnx_to_om.sh face
#   bash Scripts/ascend_310b/convert_onnx_to_om.sh pose
#
# Common overrides:
#   SMART_IP_SOC_VERSION=Ascend310B1
#   SMART_IP_ATC_INPUT_SHAPE='image:1,3,112,112'
#   SMART_IP_ATC_INPUT_FORMAT=NCHW
#   SMART_IP_AIPP_CONFIG=/path/to/aipp.cfg

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT_ROOT="${REPO_ROOT}/code/ascend_310B/smart_ip_interaction"
MODEL_KIND="${1:-emotion}"

SOC_VERSION="${SMART_IP_SOC_VERSION:-Ascend310B1}"
INPUT_FORMAT="${SMART_IP_ATC_INPUT_FORMAT:-NCHW}"
AIPP_CONFIG="${SMART_IP_AIPP_CONFIG:-}"

case "${MODEL_KIND}" in
  emotion)
    MODEL_PATH="${SMART_IP_ATC_MODEL:-${PROJECT_ROOT}/models/onnx/emotion_mobilenetv3_6cls.onnx}"
    OUTPUT_PREFIX="${SMART_IP_ATC_OUTPUT:-${PROJECT_ROOT}/models/om/emotion_mobilenetv3_6cls}"
    INPUT_SHAPE="${SMART_IP_ATC_INPUT_SHAPE:-image:1,3,112,112}"
    ;;
  face)
    MODEL_PATH="${SMART_IP_ATC_MODEL:-${PROJECT_ROOT}/models/pretrained/face_detection_yunet_2023mar.onnx}"
    OUTPUT_PREFIX="${SMART_IP_ATC_OUTPUT:-${PROJECT_ROOT}/models/om/face_detection_yunet_2023mar_640}"
    INPUT_SHAPE="${SMART_IP_ATC_INPUT_SHAPE:-input:1,3,640,640}"
    ;;
  pose)
    MODEL_PATH="${SMART_IP_ATC_MODEL:-${PROJECT_ROOT}/models/onnx/yolo26s-pose.onnx}"
    OUTPUT_PREFIX="${SMART_IP_ATC_OUTPUT:-${PROJECT_ROOT}/models/om/yolo26s-pose}"
    INPUT_SHAPE="${SMART_IP_ATC_INPUT_SHAPE:-images:1,3,640,640}"
    ;;
  *)
    echo "[ERROR] unknown model kind: ${MODEL_KIND}" >&2
    echo "Usage: $0 emotion|face|pose" >&2
    exit 2
    ;;
esac

if ! command -v atc >/dev/null 2>&1; then
  echo "[ERROR] atc not found. Source the Ascend CANN environment first." >&2
  echo "        Example: source Scripts/ascend_310b/set_env.sh" >&2
  exit 1
fi

if [[ ! -f "${MODEL_PATH}" ]]; then
  echo "[ERROR] ONNX model not found: ${MODEL_PATH}" >&2
  exit 1
fi

mkdir -p "$(dirname "${OUTPUT_PREFIX}")"

cmd=(
  atc
  "--model=${MODEL_PATH}"
  "--framework=5"
  "--output=${OUTPUT_PREFIX}"
  "--soc_version=${SOC_VERSION}"
  "--input_shape=${INPUT_SHAPE}"
)

if [[ -n "${INPUT_FORMAT}" ]]; then
  cmd+=("--input_format=${INPUT_FORMAT}")
fi

if [[ -n "${AIPP_CONFIG}" ]]; then
  if [[ ! -f "${AIPP_CONFIG}" ]]; then
    echo "[ERROR] AIPP config not found: ${AIPP_CONFIG}" >&2
    exit 1
  fi
  cmd+=("--insert_op_conf=${AIPP_CONFIG}")
fi

echo "[INFO] model kind   : ${MODEL_KIND}"
echo "[INFO] model        : ${MODEL_PATH}"
echo "[INFO] output prefix: ${OUTPUT_PREFIX}"
echo "[INFO] soc_version  : ${SOC_VERSION}"
echo "[INFO] input_shape  : ${INPUT_SHAPE}"
echo "[INFO] input_format : ${INPUT_FORMAT:-<default>}"
echo "[INFO] aipp_config  : ${AIPP_CONFIG:-<none>}"
printf '[INFO] command      :'
printf ' %q' "${cmd[@]}"
printf '\n'

"${cmd[@]}"

echo "[OK] generated: ${OUTPUT_PREFIX}.om"
