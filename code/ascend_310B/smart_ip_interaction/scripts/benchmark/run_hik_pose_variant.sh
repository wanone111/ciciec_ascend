#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODEL_PATH="${1:?usage: run_hik_pose_variant.sh MODEL_PATH LABEL [FRAMES]}"
LABEL="${2:?usage: run_hik_pose_variant.sh MODEL_PATH LABEL [FRAMES]}"
FRAMES="${3:-300}"
STAMP="${SMART_IP_RUN_STAMP:-$(date +%Y%m%d_%H%M%S)}"
ARTIFACT_DIR="${SMART_IP_ARTIFACT_DIR:-$PROJECT_ROOT/artifacts/pose_variants/$STAMP/$LABEL}"

if [[ ! -f "$MODEL_PATH" ]]; then
  echo "[ERROR] pose model not found: $MODEL_PATH" >&2
  exit 1
fi

mkdir -p "$ARTIFACT_DIR/log"
cd "$PROJECT_ROOT"

export SMART_IP_POSE_MODEL="$MODEL_PATH"
export SMART_IP_MAX_FRAMES="$FRAMES"
export SMART_IP_ACL_PROFILE="${SMART_IP_ACL_PROFILE:-1}"
export SMART_IP_STREAM_ENABLED="${SMART_IP_STREAM_ENABLED:-1}"
export SMART_IP_BENCH_DIR="$ARTIFACT_DIR/log"

set +e
bash scripts/start_hik_direct.sh > "$ARTIFACT_DIR/console.log" 2>&1
rc=$?
set -e
printf '%s\n' "$rc" > "$ARTIFACT_DIR/exit_status.txt"
printf '%s\n' "$MODEL_PATH" > "$ARTIFACT_DIR/model_path.txt"
sha256sum "$MODEL_PATH" > "$ARTIFACT_DIR/model_sha256.txt"
echo "$ARTIFACT_DIR"
exit "$rc"
