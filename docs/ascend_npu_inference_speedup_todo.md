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

- [ ] 优化 `AclOmSession.execute()` 的输入拷贝路径。
  - 当前位置：`src/inference/acl_om.py`
  - 当前问题：每帧 `np.ascontiguousarray -> arr.tobytes() -> bytes_to_ptr -> memcpy`
  - 建议改动：复用 host buffer，减少中间 `bytes` 对象和 Python copy。
- [ ] 优化输出读取路径。
  - 当前问题：`ptr_to_bytes -> np.frombuffer(...).copy()`
  - 建议改动：评估是否可以复用 host output buffer，减少额外 copy。
- [x] 在 `AclOmSession` 中加入可选 profiling。
  - 建议变量：`SMART_IP_ACL_PROFILE=1`
  - 输出字段：`acl_h2d_ms`、`acl_execute_ms`、`acl_d2h_ms`
  - 已覆盖通用 `AclOmSession` 和姿态内部 `_AclOmSession`，逐帧日志会写出 `pose_*`、`face_*`、`expression_*` 前缀字段。
- [ ] 统一姿态和通用 ACL wrapper。
  - 当前现象：`pose_infer.py` 内部还有一份 `_AclOmSession` 风格实现，`acl_om.py` 也有通用实现。
  - 目标：减少重复代码，避免两个 wrapper 优化不一致。

## 3. CPU 预处理优化

- [ ] 梳理三个模型的 OM 输入契约。
  - 姿态：输入 dims、layout、dtype、是否已带 AIPP
  - 人脸：输入 dims、layout、dtype、resize 方式
  - 表情：输入 dims、layout、dtype、归一化方式
- [ ] 优化姿态 CPU 预处理。
  - 当前位置：`src/inference/pose_infer.py`
  - 当前操作：letterbox、BGR2RGB、float32/uint8 转换、HWC/NCHW 转换
  - 短期目标：减少重复 allocation，复用中间 buffer。
- [ ] 优化人脸 CPU 预处理。
  - 当前位置：`src/inference/face_detector.py`
  - TODO：检查 resize、颜色转换、layout 转换是否和 OM 输入契约完全匹配。
- [ ] 优化表情 CPU 预处理。
  - 当前位置：`src/inference/expression_infer.py`
  - 当前操作：BGR2RGB、resize、float32/uint8 转换、transpose
  - 短期目标：复用 buffer，减少小图频繁分配。

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
- [ ] 补充 ATC/AIPP 配置文件和转换命令。
  - 建议目录：`Scripts/ascend_310b/`
  - 产物：`*.cfg`、转换说明、模型输入输出校验脚本。

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

- [ ] 评估姿态输入尺寸从 `640` 降到 `512` 或 `416`。
  - 涉及位置：`PoseInfer(imgsz=640)`、OM 转换产物、动作阈值复核。
  - 风险：关键点精度下降，需要现场动作准确率复测。
- [ ] 人脸检测改为 tracking + 低频检测。
  - 目标：不是每次表情分支都跑 YuNet；稳定场景下复用上一帧 bbox。
  - 需要新增：bbox 失效条件、周期性重检测、置信度阈值。
- [ ] 表情分支自适应跳帧。
  - 目标：无人脸/低置信度时降频，检测到稳定人脸时短时间升频。
- [ ] 动作事件触发后短暂降低姿态后处理频率。
  - 目标：互动视频播放期间减少无效事件计算。
  - 注意：不要影响动作释放和重新触发逻辑。

## 7. 文档与验证

- [x] README 增加提速参数说明。
  - 涉及：`SMART_IP_EXPR_SKIP`、`SMART_IP_ENABLE_OVERLAY`、`SMART_IP_ENABLE_AR`、`SMART_IP_ACL_PROFILE`
- [ ] 增加压测 SOP。
  - 内容：清空/新建日志目录、启动顺序、`npu-smi info` 观察方式、汇总命令。
- [ ] 每个优化 PR/提交都记录前后数据。
  - 至少记录：平均 FPS、P95 frame_total_ms、平均 pose_ms、平均 expression_ms。
- [ ] 保留准确率回归检查。
  - 动作：`WAVE/HEART/CHEER` 各 10 次以上。
  - 表情：6 类各 10 次以上。

## 建议实施顺序

1. 完成基线和细粒度 profiling。
2. 实现运行时开关，先降低压测干扰。
3. 优化 ACL/OM wrapper 的数据搬运。
4. 做表情分支异步和低频 tracking。
5. 根据 profiling 结果再投入 DVPP/AIPP 和模型重转。
