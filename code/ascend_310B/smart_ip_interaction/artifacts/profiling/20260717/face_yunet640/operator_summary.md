# msprof summary: face_detection_yunet_2023mar_640

- Inferences: 1
- Mean device task total: 6.298 ms
- AICPU calls: 0

| op | type | task | calls | mean us/inference | share |
|---|---|---|---:|---:|---:|
| Conv_0function_graph_0/Conv_0Conv_0function_graph_0/Conv_0_pass.1 | Conv2D | AI_CORE | 1 | 1283.980 | 20.39% |
| trans_TransData_1 | TransData | AI_VECTOR_CORE | 1 | 761.460 | 12.09% |
| Conv_16function_graph_5/Conv_16Conv_16function_graph_5/Conv_16_pass.1 | Conv2D | AI_CORE | 1 | 496.420 | 7.88% |
| Conv_3function_graph_1/Conv_3Conv_3function_graph_1/Conv_3_pass.1 | Conv2D | AI_CORE | 1 | 490.600 | 7.79% |
| trans_Cast_0 | Cast | AI_VECTOR_CORE | 1 | 468.420 | 7.44% |
| Conv_2 | Conv2D | AI_CORE | 1 | 334.980 | 5.32% |
| Conv_13function_graph_4/Conv_13Conv_13function_graph_4/Conv_13_pass.1 | Conv2D | AI_CORE | 1 | 207.860 | 3.30% |
| Conv_10function_graph_3/Conv_10Conv_10function_graph_3/Conv_10_pass.1 | Conv2D | AI_CORE | 1 | 207.080 | 3.29% |
| Conv_15 | Conv2D | AI_CORE | 1 | 128.820 | 2.05% |
| Conv_20function_graph_6/Conv_20Conv_20function_graph_6/Conv_20_pass.1 | Conv2D | AI_CORE | 1 | 108.240 | 1.72% |
| Conv_52function_graph_14/Conv_52Conv_52function_graph_14/Conv_52_pass.1 | Conv2D | AI_CORE | 1 | 107.960 | 1.71% |
| Conv_23function_graph_7/Conv_23Conv_23function_graph_7/Conv_23_pass.1 | Conv2D | AI_CORE | 1 | 107.780 | 1.71% |
| MaxPool_5 | MaxPoolV3 | AI_VECTOR_CORE | 1 | 106.220 | 1.69% |
| MaxPool_18 | MaxPoolV3 | AI_VECTOR_CORE | 1 | 99.580 | 1.58% |
| Conv_12 | Conv2D | AI_CORE | 1 | 76.440 | 1.21% |
