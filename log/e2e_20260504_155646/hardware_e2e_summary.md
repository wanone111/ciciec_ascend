# Hardware End-to-End Benchmark Summary

- PYNQ log directory: `/home/bb/competition_ws/ciciec_ascend/log/e2e_20260504_155646`
- 310B log directory: `/home/bb/competition_ws/ciciec_ascend/log/e2e_20260504_155646`
- PC log directory: `/home/bb/competition_ws/ciciec_ascend/log/e2e_20260504_155646`
- Duration: 51853.68 s
- Processed frames: 498
- Pipeline FPS by duration: 0.01

## PYNQ Video Source

| Metric | Result |
|---|---|
| Sent frames | 1184 |
| Last frame id | 660 |
| Total sent packets | 13781 |
| Send failed packets | 0 |
| Failed fragments in frame logs | 0 |

## Performance

| Metric | Result |
|---|---|
| Reported FPS | avg 11.60, p50 11.93, p95 14.87, max 16.99 |
| Pose inference | avg 45.65 ms, p50 44.84 ms, p95 51.32 ms, max 61.09 ms |
| Expression branch | avg 24.09 ms, p50 23.10 ms, p95 31.95 ms, max 35.33 ms |
| Per-frame total | avg 69.99 ms, p50 65.55 ms, p95 94.80 ms, max 114.87 ms |
| Frame gap | avg 89.53 ms, p50 83.76 ms, p95 124.52 ms, max 198.27 ms |

## Latency

| Segment | Result |
|---|---|
| 310B pipeline -> PC backend | N/A |
| Backend broadcast -> player receive | avg 1.01 ms, p50 0.95 ms, p95 1.40 ms, max 1.81 ms |
| 310B pipeline -> player switch | N/A |
| PYNQ capture -> player switch | avg 4808.94 ms, p50 4587.29 ms, p95 9440.68 ms, max 9581.54 ms |

## UDP Link

| Metric | Result |
|---|---|
| Reassembled frames | 410 |
| Packet loss | 0.00% |
| Frame loss | 0.00% |
| Assembly latency avg | 80.77 ms |
| Sender latency avg | 4859.17 ms |
| Dropped frames/fragments | 0 / 0 |

## Events And Stability

| Metric | Result |
|---|---|
| Action / expression events | 48 / 28 |
| Player video switches | 16 |
| Backend suppressed events | 42 |
| WebSocket disconnects | 1 |
| Missing video commands | 26 |
| Max frame gap | 198.27 ms |

## Action Accuracy

- Trials: 3
- Accuracy: 0.00%
- Outside-window false positives: 48

| Class | Precision | Recall | F1 | Support |
|---|---:|---:|---:|---:|
| CHEER | N/A | 0.00% | N/A | 1 |
| HEART | N/A | 0.00% | N/A | 1 |
| WAVE | N/A | 0.00% | N/A | 1 |

## Expression Accuracy

- Trials: 6
- Accuracy: 0.00%
- Outside-window false positives: 28

| Class | Precision | Recall | F1 | Support |
|---|---:|---:|---:|---:|
| angry | N/A | 0.00% | N/A | 1 |
| fear | N/A | 0.00% | N/A | 1 |
| happy | N/A | 0.00% | N/A | 1 |
| neutral | N/A | 0.00% | N/A | 1 |
| sad | N/A | 0.00% | N/A | 1 |
| surprised | N/A | 0.00% | N/A | 1 |
