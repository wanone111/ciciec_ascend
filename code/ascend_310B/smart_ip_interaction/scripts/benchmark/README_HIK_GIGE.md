# Hikrobot GigE benchmark commands

```bash
cd /home/HwHiAiUser/Desktop/ciciec_ascend-main/code/ascend_310B/smart_ip_interaction

# Full pipeline, 300 frames
SMART_IP_MAX_FRAMES=300 \
SMART_IP_ACL_PROFILE=1 \
SMART_IP_BENCH_DIR="$PWD/artifacts/optimized/manual/log" \
bash scripts/start_hik_direct.sh

# Audit checksums and tensor contracts
source /usr/local/Ascend/ascend-toolkit/set_env.sh
python3 scripts/benchmark/audit_om_contracts.py \
  --output artifacts/contracts/manual/contracts.json

# ACL microbenchmark
python3 scripts/benchmark/benchmark_om.py \
  --model models/om/yolo26s-pose.om --warmup 10 --runs 50

# msprof collection and Top-K aggregation
bash scripts/benchmark/profile_om.sh \
  models/om/yolo26s-pose.om artifacts/profiling/manual/pose
python3 scripts/benchmark/summarize_msprof.py \
  artifacts/profiling/manual/pose \
  --json-output artifacts/profiling/manual/pose/operator_summary.json \
  --markdown-output artifacts/profiling/manual/pose/operator_summary.md
```

Do not run multiple `msprof` jobs concurrently on the same NPU. Accuracy gates
must use labeled real people/actions; random-input microbenchmarks are only for
runtime and operator timing.
