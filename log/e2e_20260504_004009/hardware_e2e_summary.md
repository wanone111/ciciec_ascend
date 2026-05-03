# Hardware End-to-End Benchmark Summary

- PYNQ log directory: `/home/bb/competition_ws/ciciec_ascend/log/e2e_20260504_004009`
- 310B log directory: `/home/bb/competition_ws/ciciec_ascend/log/e2e_20260504_004009`
- PC log directory: `/home/bb/competition_ws/ciciec_ascend/log/e2e_20260504_004009`
- Duration: 3275.77 s
- Processed frames: 45
- Pipeline FPS by duration: 0.01

## PYNQ Video Source

| Metric | Result |
|---|---|
| Sent frames | 494 |
| Last frame id | 480 |
| Total sent packets | 11402 |
| Send failed packets | 0 |
| Failed fragments in frame logs | 0 |

## Performance

| Metric | Result |
|---|---|
| Reported FPS | avg 0.63, p50 0.67, p95 0.72, max 0.76 |
| Pose inference | avg 1466.84 ms, p50 1452.60 ms, p95 1698.51 ms, max 1935.44 ms |
| Expression branch | avg 132.80 ms, p50 128.78 ms, p95 166.48 ms, max 178.60 ms |
| Per-frame total | avg 1532.22 ms, p50 1488.36 ms, p95 1832.32 ms, max 1985.20 ms |
| Frame gap | avg 10861.52 ms, p50 1499.85 ms, p95 1840.37 ms, max 412011.46 ms |

## Latency

| Segment | Result |
|---|---|
| 310B pipeline -> PC backend | N/A |
| Backend broadcast -> player receive | avg 1.03 ms, p50 0.98 ms, p95 1.43 ms, max 1.51 ms |
| 310B pipeline -> player switch | N/A |
| PYNQ capture -> player switch | avg 7036.58 ms, p50 6677.73 ms, p95 12806.64 ms, max 13819.66 ms |

## UDP Link

| Metric | Result |
|---|---|
| Reassembled frames | 259 |
| Packet loss | 0.00% |
| Frame loss | 0.00% |
| Assembly latency avg | 107.43 ms |
| Sender latency avg | 5966.54 ms |
| Dropped frames/fragments | 0 / 0 |

## Events And Stability

| Metric | Result |
|---|---|
| Action / expression events | 22 / 30 |
| Player video switches | 10 |
| Backend suppressed events | 6 |
| WebSocket disconnects | 1 |
| Missing video commands | 14 |
| Max frame gap | 412011.46 ms |

## Action Accuracy

- Trials: 3
- Accuracy: 0.00%
- Outside-window false positives: 22

| Class | Precision | Recall | F1 | Support |
|---|---:|---:|---:|---:|
| CHEER | N/A | 0.00% | N/A | 1 |
| HEART | N/A | 0.00% | N/A | 1 |
| WAVE | N/A | 0.00% | N/A | 1 |

## Expression Accuracy

- Trials: 6
- Accuracy: 0.00%
- Outside-window false positives: 30

| Class | Precision | Recall | F1 | Support |
|---|---:|---:|---:|---:|
| angry | N/A | 0.00% | N/A | 1 |
| fear | N/A | 0.00% | N/A | 1 |
| happy | N/A | 0.00% | N/A | 1 |
| neutral | N/A | 0.00% | N/A | 1 |
| sad | N/A | 0.00% | N/A | 1 |
| surprised | N/A | 0.00% | N/A | 1 |
