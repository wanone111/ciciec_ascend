#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODEL_PATH="${1:?usage: profile_om.sh MODEL_PATH [OUTPUT_DIR]}"
OUTPUT_DIR="${2:-$PROJECT_ROOT/artifacts/profiling/$(date +%Y%m%d_%H%M%S)/$(basename "$MODEL_PATH" .om)}"
MSPROF="${MSPROF:-/usr/local/Ascend/ascend-toolkit/7.0.RC1/tools/profiler/bin/msprof}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
WARMUP="${SMART_IP_PROFILE_WARMUP:-5}"
RUNS="${SMART_IP_PROFILE_RUNS:-20}"

cd "$PROJECT_ROOT"
source /usr/local/Ascend/ascend-toolkit/set_env.sh
mkdir -p "$OUTPUT_DIR"

{
  date --iso-8601=seconds
  uname -a
  npu-smi info
  "$PYTHON_BIN" --version
  "$MSPROF" --version 2>&1 || true
  sha256sum "$MODEL_PATH"
  env | grep -E '^(ASCEND|LD_LIBRARY_PATH|PYTHONPATH)=' | sort
} > "$OUTPUT_DIR/environment.txt"

APP="$PYTHON_BIN $PROJECT_ROOT/scripts/benchmark/benchmark_om.py --model $MODEL_PATH --warmup $WARMUP --runs $RUNS --json-output $OUTPUT_DIR/microbenchmark.json"
"$MSPROF" \
  --output="$OUTPUT_DIR/raw" \
  --application="$APP" \
  --ascendcl=on \
  --model-execution=on \
  --runtime-api=on \
  --task-time=on \
  --aicpu=on \
  --sys-profiling=off \
  --analyze=off \
  --export=on \
  > "$OUTPUT_DIR/msprof.console.log" 2>&1

echo "$OUTPUT_DIR"
