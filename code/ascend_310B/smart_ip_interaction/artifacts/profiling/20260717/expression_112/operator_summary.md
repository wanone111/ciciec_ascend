# msprof summary: emotion_mobilenetv3_6cls

- Inferences: 1
- Mean device task total: 0.900 ms
- AICPU calls: 0

| op | type | task | calls | mean us/inference | share |
|---|---|---|---:|---:|---:|
| trans_TransData_263PartitionedCall_/classifier/classifier.0/Gemm_MatMulV2_3 | MatMulV2 | AI_CORE | 1 | 98.200 | 10.91% |
| trans_TransData_1 | TransData | AI_VECTOR_CORE | 1 | 32.640 | 3.63% |
| /features/features.11/block/block.1/block.1.0/Conv | Conv2D | AI_CORE | 1 | 31.340 | 3.48% |
| /features/features.10/block/block.1/block.1.0/Conv | Conv2D | AI_CORE | 1 | 24.200 | 2.69% |
| /features/features.7/block/block.3/block.3.0/Conv | Conv2D | AI_CORE | 1 | 23.360 | 2.60% |
| /features/features.4/block/block.3/block.3.0/Conv | Conv2D | AI_CORE | 1 | 21.940 | 2.44% |
| trans_Cast_0 | Cast | AI_VECTOR_CORE | 1 | 20.100 | 2.23% |
| /features/features.0/features.0.0/Conv | Conv2D | AI_CORE | 1 | 19.020 | 2.11% |
| /features/features.5/block/block.1/block.1.0/Conv | Conv2D | AI_CORE | 1 | 17.960 | 2.00% |
| /features/features.6/block/block.1/block.1.0/Conv | Conv2D | AI_CORE | 1 | 17.760 | 1.97% |
| /features/features.0/features.0.2/HardSigmoid/features/features.0/features.0.2/Mul | AutomaticBufferFusionOp | AI_VECTOR_CORE | 1 | 14.420 | 1.60% |
| /features/features.10/block/block.3/block.3.0/Convfunction_graph_17//features/features.10/block/block.3/block.3.0/Conv/features/features.10/block/block.3/block.3.0/Convfunction_graph_17//features/features.10/block/block.3/block.3.0/Conv_pass.1 | Conv2D | AI_CORE | 1 | 12.980 | 1.44% |
| /features/features.5/block/block.3/block.3.0/Convfunction_graph_9//features/features.5/block/block.3/block.3.0/Conv/features/features.5/block/block.3/block.3.0/Convfunction_graph_9//features/features.5/block/block.3/block.3.0/Conv_pass.1 | Conv2D | AI_CORE | 1 | 12.340 | 1.37% |
| /features/features.10/block/block.2/fc1/Convfunction_graph_16//features/features.10/block/block.2/fc1/Conv/features/features.10/block/block.2/fc1/Convfunction_graph_16//features/features.10/block/block.2/fc1/Conv_pass.1 | FullyConnection | AI_CORE | 1 | 12.140 | 1.35% |
| /features/features.11/block/block.3/block.3.0/Convfunction_graph_19//features/features.11/block/block.3/block.3.0/Conv/features/features.11/block/block.3/block.3.0/Convfunction_graph_19//features/features.11/block/block.3/block.3.0/Conv_pass.1 | Conv2D | AI_CORE | 1 | 12.140 | 1.35% |
