#!/usr/bin/env bash
set -euo pipefail

# Compatible base conversion:
#   bash Scripts/ascend_310b/convert_onnx_to_om.sh pose
# New non-overwriting AIPP variants:
#   bash Scripts/ascend_310b/convert_onnx_to_om.sh pose aipp_bgr
#   bash Scripts/ascend_310b/convert_onnx_to_om.sh pose aipp_nv12

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
APP_ROOT="${REPO_ROOT}/code/ascend_310B/smart_ip_interaction"
MODEL_KIND="${1:-}"
VARIANT="${2:-${SMART_IP_ATC_VARIANT:-base}}"
SOC_VERSION="${SOC_VERSION:-Ascend310B1}"

if [[ -z "${MODEL_KIND}" ]]; then
  echo "usage: $0 pose|pose_n|emotion|expression|face [base|audit_float|aipp_bgr|aipp_nv12]" >&2
  exit 2
fi

case "${MODEL_KIND}" in
  pose)
    ONNX_PATH="${SMART_IP_POSE_ONNX:-${APP_ROOT}/models/onnx/yolo26s-pose.onnx}"
    BASE_NAME="yolo26s-pose"
    INPUT_SHAPE="1,3,640,640"
    LOGICAL_SIZE=640
    ;;
  pose_n|pose_n640)
    ONNX_PATH="${SMART_IP_POSE_ONNX:-${APP_ROOT}/models/onnx/yolo26n-pose-640.onnx}"
    BASE_NAME="yolo26n-pose-640"
    INPUT_SHAPE="1,3,640,640"
    LOGICAL_SIZE=640
    MODEL_KIND="pose"
    ;;
  emotion|expression)
    ONNX_PATH="${SMART_IP_EXPRESSION_ONNX:-${APP_ROOT}/models/onnx/emotion_mobilenetv3_6cls.onnx}"
    BASE_NAME="emotion_mobilenetv3_6cls"
    INPUT_SHAPE="1,3,112,112"
    LOGICAL_SIZE=112
    MODEL_KIND="expression"
    ;;
  face)
    ONNX_PATH="${SMART_IP_FACE_ONNX:-${APP_ROOT}/models/pretrained/face_detection_yunet_2023mar.onnx}"
    BASE_NAME="face_detection_yunet_2023mar_640"
    INPUT_SHAPE="1,3,640,640"
    LOGICAL_SIZE=640
    ;;
  *) echo "unsupported model kind: ${MODEL_KIND}" >&2; exit 2 ;;
esac

if [[ ! -f "${ONNX_PATH}" ]]; then
  echo "ONNX model not found: ${ONNX_PATH}" >&2
  echo "Set SMART_IP_POSE_ONNX/SMART_IP_EXPRESSION_ONNX/SMART_IP_FACE_ONNX to the audited model." >&2
  exit 3
fi

if [[ -n "${SMART_IP_ONNX_INPUT_NAME:-}" ]]; then
  INPUT_NAME="${SMART_IP_ONNX_INPUT_NAME}"
  echo "Using explicit audited ONNX input name: ${INPUT_NAME}"
else
  INPUT_NAME="$(python3 - "${ONNX_PATH}" <<'PY'
import sys
try:
    import onnx
except ImportError as exc:
    raise SystemExit(
        "python package 'onnx' is required for automatic input audit; alternatively set "
        "SMART_IP_ONNX_INPUT_NAME to a name confirmed by audit_om_contracts.py"
    ) from exc
model = onnx.load(sys.argv[1], load_external_data=False)
initializers = {item.name for item in model.graph.initializer}
inputs = [item.name for item in model.graph.input if item.name not in initializers]
if len(inputs) != 1:
    raise SystemExit(f"expected exactly one runtime input, found {inputs}")
print(inputs[0])
PY
)"
fi
INPUT_SHAPE="${SMART_IP_ONNX_INPUT_SHAPE:-${INPUT_SHAPE}}"

AIPP_CFG=""
case "${MODEL_KIND}:${VARIANT}" in
  pose:aipp_bgr) AIPP_CFG="${SCRIPT_DIR}/aipp/pose_bgr_u8_640.cfg"; SUFFIX="_aipp_bgr_u8" ;;
  pose:aipp_nv12) AIPP_CFG="${SCRIPT_DIR}/aipp/pose_nv12_fullrange_640.cfg"; SUFFIX="_aipp_nv12" ;;
  expression:aipp_bgr) AIPP_CFG="${SCRIPT_DIR}/aipp/expression_bgr_u8_112.cfg"; SUFFIX="_aipp_bgr_u8" ;;
  expression:aipp_nv12) AIPP_CFG="${SCRIPT_DIR}/aipp/expression_nv12_fullrange_112.cfg"; SUFFIX="_aipp_nv12" ;;
  face:aipp_nv12) AIPP_CFG="${SCRIPT_DIR}/aipp/face_nv12_fullrange_640.cfg"; SUFFIX="_aipp_nv12" ;;
  *:audit_float) SUFFIX="_audit_float" ;;
  *:base) SUFFIX="" ;;
  *) echo "unsupported variant ${MODEL_KIND}:${VARIANT}" >&2; exit 2 ;;
esac

OUTPUT_STEM="${APP_ROOT}/models/om/${BASE_NAME}${SUFFIX}"
OUTPUT_OM="${OUTPUT_STEM}.om"
if [[ -e "${OUTPUT_OM}" && "${ALLOW_OVERWRITE:-0}" != "1" ]]; then
  echo "refusing to overwrite existing model: ${OUTPUT_OM}" >&2
  exit 4
fi

ATC_CMD=(atc --framework=5 --model="${ONNX_PATH}" --output="${OUTPUT_STEM}"
  --input_shape="${INPUT_NAME}:${INPUT_SHAPE}" --soc_version="${SOC_VERSION}")
if [[ -n "${AIPP_CFG}" ]]; then
  ATC_CMD+=(--insert_op_conf="${AIPP_CFG}")
fi

printf 'ATC command:'
printf ' %q' "${ATC_CMD[@]}"
printf '\n'
if [[ "${DRY_RUN:-0}" != "1" ]]; then
  "${ATC_CMD[@]}"
fi

if [[ ! -f "${OUTPUT_OM}" ]]; then
  [[ "${DRY_RUN:-0}" == "1" ]] && exit 0
  echo "ATC did not produce ${OUTPUT_OM}" >&2
  exit 5
fi

TOOLKIT_ROOT="${ASCEND_TOOLKIT_HOME:-/usr/local/Ascend/ascend-toolkit/latest}"
TOOLKIT_INFO=""
for candidate in \
  "${TOOLKIT_ROOT}/aarch64-linux/ascend_toolkit_install.info" \
  "${TOOLKIT_ROOT}/x86_64-linux/ascend_toolkit_install.info" \
  "${TOOLKIT_ROOT}/ascend_toolkit_install.info"; do
  if [[ -f "${candidate}" ]]; then
    TOOLKIT_INFO="${candidate}"
    break
  fi
done
if [[ -n "${TOOLKIT_INFO}" ]]; then
  ATC_VERSION="$(grep -E '^(version|package_name)=' "${TOOLKIT_INFO}" | paste -sd ';' -)"
else
  ATC_VERSION="$(atc --version 2>&1 || atc --help 2>&1 | head -n 1 || true)"
fi
META_PATH="${OUTPUT_STEM}.metadata.json"
python3 - "${ONNX_PATH}" "${OUTPUT_OM}" "${AIPP_CFG}" "${META_PATH}" "${INPUT_NAME}" "${INPUT_SHAPE}" "${SOC_VERSION}" "${ATC_VERSION}" "${VARIANT}" "${ATC_CMD[*]}" <<'PY'
import hashlib, json, pathlib, sys
onnx_path, om_path, cfg_path, meta_path, input_name, shape, soc, atc_version, variant, command = sys.argv[1:]
def sha(path):
    if not path:
        return None
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()
record = {
    "variant": variant, "soc_version": soc, "atc_version": atc_version,
    "atc_command": command, "input_name": input_name, "onnx_input_shape": shape,
    "onnx": {"path": str(pathlib.Path(onnx_path).resolve()), "sha256": sha(onnx_path)},
    "om": {"path": str(pathlib.Path(om_path).resolve()), "sha256": sha(om_path)},
    "aipp_cfg": None if not cfg_path else {"path": str(pathlib.Path(cfg_path).resolve()), "sha256": sha(cfg_path)},
    "status": "converted_not_yet_parity_validated" if cfg_path else "converted",
}
pathlib.Path(meta_path).write_text(json.dumps(record, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(json.dumps(record, indent=2, ensure_ascii=False))
PY

AUDIT="${APP_ROOT}/scripts/benchmark/audit_om_contracts.py"
if [[ -f "${AUDIT}" ]]; then
  case "${VARIANT}" in
    aipp_bgr) AUDIT_MODE=aipp_bgr_u8 ;;
    aipp_nv12) AUDIT_MODE=aipp_nv12 ;;
    *) AUDIT_MODE=float_nchw ;;
  esac
  PYTHONPATH="${APP_ROOT}/src${PYTHONPATH:+:${PYTHONPATH}}" python3 "${AUDIT}" \
    "${OUTPUT_OM}" --mode "${AUDIT_MODE}" --logical-size "${LOGICAL_SIZE}" \
    --output "${OUTPUT_STEM}.contract.json"
else
  echo "warning: OM audit script not found: ${AUDIT}" >&2
fi
