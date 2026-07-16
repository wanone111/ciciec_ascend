# 真实硬件端到端压测说明

本目录用于采集和汇总 PYNQ 实时视频、Ascend 310B 推理、PC 后端和 IP 播放器的端到端指标。

## 采集方式

默认日志目录按三端真实部署路径固定：

| 端侧 | 默认目录 | 日志文件 |
|---|---|---|
| PYNQ-Z2 | `/home/xilinx/jupyter_notebooks/ciciec/log` | `pynq.jsonl` |
| Ascend 310B | `/root/userdata/ciciec/ascend_310B/log/` | `pipeline.jsonl` |
| PC 后端/播放器 | `/home/bb/competition_ws/ciciec_ascend/log` | `backend.jsonl`、`player.jsonl` |

一般不需要再设置 `SMART_IP_BENCH_DIR`。如果现场要临时换目录，可以用 `SMART_IP_PYNQ_BENCH_DIR`、`SMART_IP_310B_BENCH_DIR`、`SMART_IP_PC_BENCH_DIR` 分别覆盖，或用 `SMART_IP_BENCH_DIR` 让当前进程写到同一个目录。

```bash
export SMART_IP_UDP_MODULE_PATH=/home/bb/competition_ws/ciciec_ascend/code/communication/build
export SMART_IP_BACKEND_HOST=192.168.137.1
export SMART_IP_USE_UDP=1
```

纯推理压测时建议关闭可视化和 AR，并开启 ACL 分段 profiling：

```bash
export SMART_IP_ENABLE_OVERLAY=0
export SMART_IP_ENABLE_AR=0
export SMART_IP_ACL_PROFILE=1
export SMART_IP_BENCH_FRAME_STRIDE=5
```

如果现场人脸位置稳定，可以降低 YuNet 调用频率，复用最近 bbox：

```bash
export SMART_IP_EXPR_SKIP=3
export SMART_IP_FACE_DETECT_INTERVAL=2
export SMART_IP_FACE_TRACK_TTL=6
```

每轮压测建议使用新目录，避免旧 JSONL 污染统计：

```bash
export SMART_IP_BENCH_DIR=/tmp/smart_ip_bench_$(date +%Y%m%d_%H%M%S)
```

真实硬件链路建议先做时钟同步，否则 PYNQ 采集时间到 PC 播放器切换时间无法直接相减：

```bash
timedatectl status
chronyc tracking
```

运行顺序：

1. PC 启动 `python backend/server.py`。
2. PC 启动 `python scripts/video_player.py`。
3. 310B 启动 `python run_pipeline.py`。
4. PYNQ 启动 `code/communication/build/pynq_udp` 发送实时视频。
5. 按测试脚本依次做挥手、比心、欢呼和 6 类表情动作，并记录每个 trial 的开始/结束 Unix 时间戳。

## 标注 CSV

动作准确率使用 `action_trials_template.csv` 格式：

```csv
trial_id,expected,start_ts,end_ts
1,WAVE,1746260000.000,1746260003.000
```

表情准确率使用 `expression_trials_template.csv` 格式：

```csv
trial_id,expected,start_ts,end_ts
1,happy,1746260100.000,1746260103.000
```

`expected` 支持动作 `WAVE`、`HEART`、`CHEER`，表情 `angry`、`fear`、`happy`、`neutral`、`sad`、`surprised`。

## 汇总

```bash
python scripts/benchmark/summarize_hardware_e2e.py \
  --pynq-log-dir /home/xilinx/jupyter_notebooks/ciciec/log \
  --ascend-log-dir /root/userdata/ciciec/ascend_310B/log \
  --pc-log-dir /home/bb/competition_ws/ciciec_ascend/log \
  --action-trials scripts/benchmark/action_trials_template.csv \
  --expression-trials scripts/benchmark/expression_trials_template.csv
```

如果已经把四个 JSONL 文件拷贝到同一个目录，也可以继续使用：

```bash
python scripts/benchmark/summarize_hardware_e2e.py --log-dir /tmp/smart_ip_bench_YYYYmmdd_HHMMSS
```

在 PC 上汇总时，如果 `/home/xilinx/...` 或 `/root/userdata/...` 没有通过网络挂载直接可读，先把 PYNQ 的 `pynq.jsonl` 和 310B 的 `pipeline.jsonl` 拷贝到 PC 可访问目录，再用 `--log-dir` 或对应的 `--pynq-log-dir`、`--ascend-log-dir` 指向拷贝后的路径。

输出：

- `hardware_e2e_summary.md`：可直接摘入比赛报告的 Markdown 表格。
- `hardware_e2e_summary.json`：机器可读统计结果。

## ACL profiling 汇总

如果 310B 运行前开启：

```bash
export SMART_IP_ACL_PROFILE=1
```

ACL 输出默认直接复用 pinned host buffer 的 NumPy 视图，减少 `ptr_to_bytes` 和额外 copy。如需让输出数组跨越同一 session 的下一次推理继续有效，或排查 CANN 版本兼容性，可关闭零拷贝：

```bash
export SMART_IP_ACL_ZERO_COPY_OUTPUT=0
```

CPU 预处理默认复用姿态/人脸 NCHW 输入 buffer，并融合表情 resize、换色、归一化和 layout 转换。做兼容路径 A/B 时可关闭：

```bash
export SMART_IP_REUSE_INPUT_BUFFER=0
export SMART_IP_EXPRESSION_FUSED_PREPROCESS=0
```

`pipeline.jsonl` 的 `frame` 记录会包含 `pose_acl_*`、`face_acl_*`、`expression_acl_*` 字段。可用下面脚本汇总 NPU 执行和 Host/Device 搬运耗时：

```bash
python scripts/benchmark/summarize_acl_profile.py \
  --pipeline-log /root/userdata/ciciec/ascend_310B/log/pipeline.jsonl
```

输出字段包括 `acl_input_pack_ms`、`acl_h2d_ms`、`acl_execute_ms`、`acl_d2h_ms`、`acl_output_decode_ms` 和 `acl_total_ms` 的平均值、P50、P95 与最大值。

如果不提供 trial CSV，脚本仍会汇总 FPS、分段延迟、UDP 丢包、视频切换次数和稳定性，但不会计算动作/表情准确率。

## 日志目录注意事项

正式压测建议每轮使用新的 `SMART_IP_BENCH_DIR`，或在三端启动前清空旧的 `pynq.jsonl`、`pipeline.jsonl`、`backend.jsonl`、`player.jsonl`。如果多次启动追加到同一个目录，汇总脚本会把旧 session 的时间跨度也算进去，导致总时长、FPS、跨设备延迟被污染。

如果已经出现同目录多次追加，需要按最后一次成功 `pipeline_start`、对应的 PYNQ start 和 PC/backend/player 事件手动截取后再计算，或把本轮 JSONL 单独整理到新的目录。

`action_trials_template.csv` 和 `expression_trials_template.csv` 只是格式模板，里面的时间戳不能直接当作准确率标注。使用模板时间戳会得到 0% 或大量 outside-window 结果；正式准确率必须记录每个动作/表情 trial 的真实 Unix `start_ts` 和 `end_ts`。
