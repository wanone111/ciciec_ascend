# Hardware End-to-End Benchmark Summary

- PYNQ log directory: `/home/bb/competition_ws/ciciec_ascend/log/e2e_20260504_151614`
- 310B log directory: `/home/bb/competition_ws/ciciec_ascend/log/e2e_20260504_151614`
- PC log directory: `/home/bb/competition_ws/ciciec_ascend/log/e2e_20260504_151614`
- Duration: 49485.68 s
- Processed frames: 318
- Pipeline FPS by duration: 0.01

## PYNQ Video Source

| Metric | Result |
|---|---|
| Sent frames | 860 |
| Last frame id | 840 |
| Total sent packets | 18406 |
| Send failed packets | 0 |
| Failed fragments in frame logs | 0 |

## Performance

| Metric | Result |
|---|---|
| Reported FPS | avg 11.05, p50 12.46, p95 15.85, max 17.43 |
| Pose inference | avg 46.16 ms, p50 44.92 ms, p95 53.17 ms, max 144.18 ms |
| Expression branch | avg 71.48 ms, p50 54.71 ms, p95 145.22 ms, max 196.62 ms |
| Per-frame total | avg 84.26 ms, p50 63.16 ms, p95 170.48 ms, max 264.30 ms |
| Frame gap | avg 102.06 ms, p50 79.89 ms, p95 190.46 ms, max 278.70 ms |

## Latency

| Segment | Result |
|---|---|
| 310B pipeline -> PC backend | N/A |
| Backend broadcast -> player receive | avg 0.95 ms, p50 0.94 ms, p95 1.39 ms, max 1.52 ms |
| 310B pipeline -> player switch | N/A |
| PYNQ capture -> player switch | N/A |

## UDP Link

| Metric | Result |
|---|---|
| Reassembled frames | 276 |
| Packet loss | 0.00% |
| Frame loss | 0.00% |
| Assembly latency avg | 84.72 ms |
| Sender latency avg | 54267025.62 ms |
| Dropped frames/fragments | 0 / 0 |

## Events And Stability

| Metric | Result |
|---|---|
| Action / expression events | 31 / 18 |
| Player video switches | 14 |
| Backend suppressed events | 24 |
| WebSocket disconnects | 1 |
| Missing video commands | 20 |
| Max frame gap | 278.70 ms |

## Action Accuracy

- Trials: 3
- Accuracy: 0.00%
- Outside-window false positives: 31

| Class | Precision | Recall | F1 | Support |
|---|---:|---:|---:|---:|
| CHEER | N/A | 0.00% | N/A | 1 |
| HEART | N/A | 0.00% | N/A | 1 |
| WAVE | N/A | 0.00% | N/A | 1 |

## Expression Accuracy

- Trials: 6
- Accuracy: 0.00%
- Outside-window false positives: 18

| Class | Precision | Recall | F1 | Support |
|---|---:|---:|---:|---:|
| angry | N/A | 0.00% | N/A | 1 |
| fear | N/A | 0.00% | N/A | 1 |
| happy | N/A | 0.00% | N/A | 1 |
| neutral | N/A | 0.00% | N/A | 1 |
| sad | N/A | 0.00% | N/A | 1 |
| surprised | N/A | 0.00% | N/A | 1 |
