# msprof summary: yolo26s-pose

- Inferences: 1
- Mean device task total: 30.703 ms
- AICPU calls: 0

| op | type | task | calls | mean us/inference | share |
|---|---|---|---:|---:|---:|
| /model.23/GatherElements_2 | GatherElements | AI_VECTOR_CORE | 1 | 5395.240 | 17.57% |
| /model.0/conv/Conv/model.0/act/Sigmoid/model.0/act/Mul | Conv2D | AI_CORE | 1 | 1852.800 | 6.03% |
| /model.3/conv/Conv/model.3/act/Sigmoid/model.3/act/Mul | Conv2D | AI_CORE | 1 | 1143.020 | 3.72% |
| /model.5/conv/Conv/model.5/act/Sigmoid/model.5/act/Mul | Conv2D | AI_CORE | 1 | 1026.620 | 3.34% |
| /model.2/cv2/conv/Conv/model.2/cv2/act/Sigmoid/model.2/cv2/act/Mul | Conv2D | AI_CORE | 1 | 1024.980 | 3.34% |
| /model.1/conv/Conv/model.1/act/Sigmoid/model.1/act/Mul | Conv2D | AI_CORE | 1 | 987.000 | 3.21% |
| /model.16/cv1/conv/Conv/model.16/cv1/act/Sigmoid/model.16/cv1/act/Mul | Conv2D | AI_CORE | 1 | 778.140 | 2.53% |
| trans_TransData_1 | TransData | AI_VECTOR_CORE | 1 | 758.060 | 2.47% |
| PartitionedCall_/model.23/Transpose_Transpose_85 | Transpose | AI_VECTOR_CORE | 1 | 646.100 | 2.10% |
| /model.4/cv2/conv/Conv/model.4/cv2/act/Sigmoid/model.4/cv2/act/Mul | Conv2D | AI_CORE | 1 | 595.640 | 1.94% |
| /model.2/cv1/conv/Conv/model.2/cv1/act/Sigmoid/model.2/cv1/act/Mul | Conv2D | AI_CORE | 1 | 523.020 | 1.70% |
| /model.7/conv/Conv/model.7/act/Sigmoid/model.7/act/Mul | Conv2D | AI_CORE | 1 | 510.480 | 1.66% |
| /model.23/one2one_cv4.0/one2one_cv4.0.0/conv/Conv/model.23/one2one_cv4.0/one2one_cv4.0.0/act/Sigmoid/model.23/one2one_cv4.0/one2one_cv4.0.0/act/Mul | Conv2D | AI_CORE | 1 | 509.820 | 1.66% |
| /model.10/m/m.0/attn/Softmax | SoftmaxV2 | AI_VECTOR_CORE | 1 | 506.320 | 1.65% |
| /model.22/m.0/m.0.1/attn/Softmax | SoftmaxV2 | AI_VECTOR_CORE | 1 | 506.060 | 1.65% |
