# Ascend NPU 推理提速 TODO

本文记录面向 Ascend 310B 的 AI infra 推理提速任务。当前主链路已经默认使用 `ACL + OM + NPU`，后续优化重点是减少 CPU 预处理、Host/Device 搬运、串行调度和可视化/日志开销。

## 0. 基线确认

- [ ] 重新跑一次干净日志目录，确认 `pipeline.jsonl` 中三条推理路径均为 `acl_om_npu`。
  - 涉及文件：`code/ascend_310B/smart_ip_interaction/run_pipeline.py`
  - 检查字段：`pose_runtime`、`face_runtime`、`expression_runtime`
- [ ] 固定压测环境和输入源，记录当前基线。
  - 记录项：PYNQ 发送 FPS、310B 处理 FPS、`pose_ms`、`expression_ms`、`frame_total_ms`
  - 涉及脚本：`code/ascend_310B/smart_ip_interaction/scripts/benchmark/summarize_hardware_e2e.py`
- [ ] 增加更细粒度推理计时，先确认瓶颈位置。
  - 姿态：preprocess、H2D、execute、D2H、postprocess
  - 人脸：preprocess、H2D、execute、D2H、decode/NMS
  - 表情：preprocess、H2D、execute、D2H、softmax/smooth

## 1. 低风险运行时开关

- [x] 将表情分支跳帧参数改为环境变量。
  - 当前位置：`run_pipeline.py` 中 `EXPR_SKIP = 3`
  - 建议变量：`SMART_IP_EXPR_SKIP`
  - 目标：现场可快速在准确率和吞吐之间调节。
- [x] 将表情事件发送间隔改为环境变量。
  - 当前位置：`run_pipeline.py` 中 `EXPR_SEND_INTERVAL = 1.5`
  - 建议变量：`SMART_IP_EXPR_SEND_INTERVAL`
- [x] 增加可视化开关，压测时关闭绘制。
  - 涉及位置：`results[0].plot()`、`cv2.rectangle`、`cv2.putText`、`draw_overlay`
  - 建议变量：`SMART_IP_ENABLE_OVERLAY`
  - 目标：减少 CPU 绘制开销对 FPS 统计的干扰。
- [x] 增加 AR 叠加开关。
  - 涉及位置：`AROverlay` 初始化、`ar.apply(...)`
  - 建议变量：`SMART_IP_ENABLE_AR`
  - 目标：压测 NPU 推理时避免 PIL/OpenCV 叠加成本。
- [x] 将逐帧 JSONL 记录默认改为可降采样。
  - 当前已有：`SMART_IP_BENCH_FRAME_STRIDE`
  - 已在 README 中补充说明，压测可设为 `5` 或 `10`。

## 2. ACL/OM wrapper 搬运优化

- [x] 优化 `AclOmSession.execute()` 的输入拷贝路径。
  - 当前位置：`src/inference/acl_om.py`
  - 当前问题：每帧 `np.ascontiguousarray -> arr.tobytes() -> bytes_to_ptr -> memcpy`
  - 已优先使用 `acl.util.numpy_to_ptr`，不可用时回退到 `bytes_to_ptr`；profiling 新增 `acl_input_pack_ms`。
- [x] 优化输出读取路径。
  - 当前问题：`ptr_to_bytes -> np.frombuffer(...).copy()`
  - 已在模型初始化时用 `acl.util.ptr_to_numpy` 为 pinned host buffer 创建一次 NumPy 视图，逐帧直接复用；接口不可用或创建失败时自动回退到 bytes + copy。
  - 默认开启，可用 `SMART_IP_ACL_ZERO_COPY_OUTPUT=0` 关闭。零拷贝返回值只保证在同一 session 下一次 `execute()` 前有效。
  - 310B 实测见本文“2026-07-16 第一轮优化实测”。
- [x] 在 `AclOmSession` 中加入可选 profiling。
  - 建议变量：`SMART_IP_ACL_PROFILE=1`
  - 输出字段：`acl_h2d_ms`、`acl_execute_ms`、`acl_d2h_ms`
  - 已覆盖通用 `AclOmSession`，逐帧日志会写出 `pose_*`、`face_*`、`expression_*` 前缀字段。
  - 已新增 `scripts/benchmark/summarize_acl_profile.py` 汇总分位数。
- [x] 统一姿态和通用 ACL wrapper。
  - 当前现象：`pose_infer.py` 内部还有一份 `_AclOmSession` 风格实现，`acl_om.py` 也有通用实现。
  - 目标：减少重复代码，避免两个 wrapper 优化不一致。
  - 已删除姿态本地重复类，姿态、人脸、表情统一使用 `src/inference/acl_om.py`。

## 3. CPU 预处理优化

- [ ] 梳理三个模型的 OM 输入契约。
  - 姿态：输入 dims、layout、dtype、是否已带 AIPP
  - 人脸：输入 dims、layout、dtype、resize 方式
  - 表情：输入 dims、layout、dtype、归一化方式
- [x] 优化姿态 CPU 预处理。
  - 当前位置：`src/inference/pose_infer.py`
  - 当前操作：letterbox、BGR2RGB、float32/uint8 转换、HWC/NCHW 转换
  - 已复用固定 NCHW float32 输入 buffer，并从 BGR 三个通道直接原地转换/归一化，避免 RGB、astype 和 contiguous 临时数组。
  - 可用 `SMART_IP_REUSE_INPUT_BUFFER=0` 回退旧路径。
- [x] 优化人脸 CPU 预处理。
  - 当前位置：`src/inference/face_detector.py`
  - 已复用 uint8 canvas 和 NCHW float32 输入 buffer，分通道直接写入，避免每次分配两块大数组。
  - 输入与 NPU 输出逐元素对比完全一致。
- [x] 优化表情 CPU 预处理。
  - 当前位置：`src/inference/expression_infer.py`
  - 当前操作：BGR2RGB、resize、float32/uint8 转换、transpose
  - 已使用 `cv2.dnn.blobFromImage` 融合 resize、BGR/RGB、float32 归一化和 NCHW 排布。
  - 默认开启，可用 `SMART_IP_EXPRESSION_FUSED_PREPROCESS=0` 回退。

## 4. DVPP/AIPP 深度集成

- [ ] 生成带 AIPP 的姿态 OM。
  - 目标：将 BGR/RGB 转换、归一化、通道处理尽量固化到模型输入侧。
  - 涉及：ATC 转换脚本、模型输入说明、README 部署说明。
- [ ] 生成带 AIPP 的表情 OM。
  - 目标：减少 `ExpressionInfer._preprocess_om()` 中的 CPU normalize/transpose。
- [ ] 评估人脸检测是否适合 AIPP 固化。
  - 重点：YuNet 输入 resize、均值方差、layout 与后处理 decode 是否一致。
- [ ] 评估 DVPP 接入视频帧 resize/crop。
  - 当前输入来自 UDP 重组后的 OpenCV BGR frame。
  - 目标：把大图 resize/裁剪从 CPU 移到 Ascend 侧硬件通道。
- [x] 补充 ATC/AIPP 配置文件和转换命令。
  - 建议目录：`Scripts/ascend_310b/`
  - 已新增 `Scripts/ascend_310b/convert_onnx_to_om.sh`，支持 `emotion`、`face`、`pose`，并预留 `SMART_IP_AIPP_CONFIG`。
  - 后续仍需补充具体 `*.cfg` 和模型输入输出校验脚本。

## 5. 流水线并行

- [ ] 将视频读取、姿态推理、表情分支拆成队列。
  - 当前位置：`run_pipeline.py`
  - 当前模式：单线程串行处理每帧。
  - 目标：减少视频接收、CPU 后处理、NPU 推理互相等待。
- [ ] 表情分支异步化。
  - 建议：主线程保持姿态帧率，表情线程按 `SMART_IP_EXPR_SKIP` 或时间间隔消费最新帧。
  - 注意：需要保护 `expr_label`、`expr_conf`、`face_bbox` 的并发访问。
- [ ] 姿态推理异步化。
  - 目标：后续可切换 `acl.mdl.execute_async` 和 stream synchronize。
  - 前置：先完成 profiling，确认同步执行确实是瓶颈。
- [ ] 增加队列丢帧策略。
  - 建议：只保留最新帧，避免延迟堆积。
  - 目标：交互系统优先低延迟，而不是逐帧处理完整性。

## 6. 算法级轻量化

- [x] 导出并验证轻量 `yolo26n-pose` 640 OM。
  - ONNX 和 OM 已永久保存，输入/输出契约与现有模型相同：`[1,3,640,640] -> [1,300,57]`。
  - 性能已通过；动作准确率回归尚未完成，因此暂不替换生产默认 `yolo26s-pose.om`。
- [ ] 评估姿态输入尺寸从 `640` 降到 `512` 或 `416`。
  - 涉及位置：`PoseInfer(imgsz=640)`、OM 转换产物、动作阈值复核。
  - 风险：关键点精度下降，需要现场动作准确率复测。
- [x] 人脸检测改为 tracking + 低频检测。
  - 目标：不是每次表情分支都跑 YuNet；稳定场景下复用上一帧 bbox。
  - 已新增 `SMART_IP_FACE_DETECT_INTERVAL` 和 `SMART_IP_FACE_TRACK_TTL`，默认保持每次表情分支都检测，现场可提高 interval 降低 YuNet 频率。
- [ ] 表情分支自适应跳帧。
  - 目标：无人脸/低置信度时降频，检测到稳定人脸时短时间升频。
- [ ] 动作事件触发后短暂降低姿态后处理频率。
  - 目标：互动视频播放期间减少无效事件计算。
  - 注意：不要影响动作释放和重新触发逻辑。

## 7. 文档与验证

- [x] README 增加提速参数说明。
  - 涉及：`SMART_IP_EXPR_SKIP`、`SMART_IP_ENABLE_OVERLAY`、`SMART_IP_ENABLE_AR`、`SMART_IP_ACL_PROFILE`
- [x] 增加压测 SOP。
  - 内容：清空/新建日志目录、启动顺序、`npu-smi info` 观察方式、汇总命令。
  - 已在 `scripts/benchmark/README.md` 补充纯推理压测参数、ACL profiling、低频人脸检测和新日志目录建议。
- [x] 每个优化 PR/提交都记录前后数据。
  - 至少记录：平均 FPS、P95 frame_total_ms、平均 pose_ms、平均 expression_ms。
  - 第一轮 ACL 输出零拷贝已记录微基准、输出一致性与整链路稳定性；后续优化继续逐项追加。
- [ ] 保留准确率回归检查。
  - 动作：`WAVE/HEART/CHEER` 各 10 次以上。
  - 表情：6 类各 10 次以上。

## 建议实施顺序

1. 完成基线和细粒度 profiling。
2. 实现运行时开关，先降低压测干扰。
3. 优化 ACL/OM wrapper 的数据搬运。
4. 做表情分支异步和低频 tracking。
5. 根据 profiling 结果再投入 DVPP/AIPP 和模型重转。

## 2026-07-16 第一轮优化实测：ACL 输出零拷贝

测试环境为实际 Ascend 310B，三个模型均确认运行时为 `acl_om_npu`。每个模型先预热 10 次，再统计固定 float32 输入 100 次；对照组使用 `ptr_to_bytes + frombuffer + copy`，优化组复用 pinned host output 的 NumPy 视图。

| 模型 | 指标 | 优化前均值 | 优化后均值 | 降幅 |
|---|---:|---:|---:|---:|
| 姿态 | output decode | 0.1575 ms | 0.0066 ms | 95.8% |
| 姿态 | ACL total | 34.0694 ms | 33.5895 ms | 1.4% |
| 人脸 | output decode | 0.7993 ms | 0.0234 ms | 97.1% |
| 人脸 | ACL total | 9.9733 ms | 9.2818 ms | 6.9% |
| 表情 | output decode | 0.0595 ms | 0.0043 ms | 92.8% |
| 表情 | ACL total | 1.3162 ms | 1.2858 ms | 2.3% |

验证结果：

- 两种路径的三个模型输出 checksum 逐项一致。
- 本地新增 4 个单元测试，覆盖匹配 shape、shape 回退、关闭开关和 API 缺失回退。
- 310B USB 摄像头整链路复测 15 秒、约 100 帧，推理约 9.95 FPS，正常响应 SIGINT 并释放摄像头。
- NumPy host 视图在释放 ACL pinned memory 前主动清空，修复首次试验发现的退出阶段生命周期问题。
- 当前总吞吐主要受 32 ms 左右的姿态 NPU execute 和摄像头节拍限制，因此本轮重点收益是减少 CPU copy；整链路 FPS 没有显著变化。

## 2026-07-16 第二轮优化实测：CPU 预处理复用

实际 310B、USB 摄像头真实帧测试。姿态和人脸使用固定 NCHW float32 buffer；表情使用融合 OpenCV blob 预处理。开关 A/B 交替执行以降低温度和执行顺序偏差。

| 路径 | 指标 | 优化前均值 | 优化后均值 | 降幅 |
|---|---:|---:|---:|---:|
| 姿态 | preprocess | 11.95 ms | 10.09 ms | 15.5% |
| 姿态 | 模型总路径 | 46.90 ms | 45.49 ms | 3.0% |
| 人脸 | preprocess | 9.89 ms | 7.26 ms | 26.6% |
| 人脸 | 模型总路径 | 20.63 ms | 18.27 ms | 11.5% |
| 表情 | preprocess | 1.027 ms | 0.770 ms | 25.1% |

25 秒 USB 整链路 A/B（关闭 overlay、AR、MJPEG，前 30 帧不计）：

| 指标 | 优化前均值 | 优化后均值 | 变化 |
|---|---:|---:|---:|
| pose_ms | 48.04 ms | 44.91 ms | -6.5% |
| expression_ms（含人脸） | 22.53 ms | 20.86 ms | -7.4% |
| frame_total_ms | 63.00 ms | 59.21 ms | -6.0% |
| frame_total P95 | 84.66 ms | 77.76 ms | -8.2% |
| FPS | 9.99 | 9.99 | 摄像头限制，无变化 |

正确性与稳定性：姿态/人脸优化前后输入和 NPU 输出最大绝对差均为 0；表情输入最大差 `5.96e-8`、NPU 输出差为 0；两轮均正常响应 SIGINT 并释放摄像头。

本轮同时排除：

- 310B ARM OpenCV 上，640 输入的 `blobFromImage` 比原路径更慢，因此没有用于姿态和人脸。
- 普通 NumPy host memory 与 ACL pinned host memory 的 4.9 MB H2D 均约 1.633 ms，pinned input 没有收益。
- 摄像头 1280×720 MJPG 裸采集实测仅 9.49 FPS，采集线程无法提高当前吞吐上限。

## 2026-07-16 第三轮研究：轻量姿态模型

使用项目已有 `yolo26n-pose.pt`，在隔离的 `ai_infra` Conda 环境用 Ultralytics 8.4.55 导出静态 ONNX opset 11，再由 310B CANN 7.0 ATC 转换为 `Ascend310B1` OM。导出脚本为 `Scripts/ascend_310b/export_to_onnx.py`。

60 次真实帧交替 A/B：

| 模型 | preprocess | ACL/NPU | postprocess | 总路径 |
|---|---:|---:|---:|---:|
| yolo26s-pose 640 | 9.70 ms | 34.56 ms | 0.29 ms | 44.54 ms |
| yolo26n-pose 640 | 9.73 ms | 22.18 ms | 0.30 ms | 32.20 ms |

轻量模型使 ACL/NPU 阶段下降 35.8%，姿态完整路径下降 27.7%。25 秒 USB 整链路对比：

| 指标 | s640 | n640 | 变化 |
|---|---:|---:|---:|
| pose_ms | 44.91 ms | 32.75 ms | -27.1% |
| frame_total_ms | 59.21 ms | 45.98 ms | -22.3% |
| frame_total P95 | 77.76 ms | 61.36 ms | -21.1% |
| FPS | 9.99 | 10.02 | 摄像头限制，无显著变化 |

限制：本轮镜头中未触发动作事件，尚不能证明 `WAVE/HEART/CHEER` 准确率与 s640 等价。n640 作为可选模型保留，完成每类至少 10 次现场回归前不改默认模型。
