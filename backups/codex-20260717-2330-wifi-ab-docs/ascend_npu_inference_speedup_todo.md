# Ascend NPU 推理提速 TODO

本文记录面向 Ascend 310B 的 AI infra 推理提速任务。当前主链路已经默认使用 `ACL + OM + NPU`，后续优化重点是减少 CPU 预处理、Host/Device 搬运、串行调度和可视化/日志开销。

## 0. 基线确认

- [x] 重新跑一次干净日志目录，确认 `pipeline.jsonl` 中三条推理路径均为 `acl_om_npu`。
  - 涉及文件：`code/ascend_310B/smart_ip_interaction/run_pipeline.py`
  - 检查字段：`pose_runtime`、`face_runtime`、`expression_runtime`
- [x] 固定压测环境和输入源，记录当前基线。
  - 记录项：PYNQ 发送 FPS、310B 处理 FPS、`pose_ms`、`expression_ms`、`frame_total_ms`
  - 涉及脚本：`code/ascend_310B/smart_ip_interaction/scripts/benchmark/summarize_hardware_e2e.py`
- [x] 增加更细粒度推理计时，先确认瓶颈位置。
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

- [x] 梳理三个模型的 OM 输入契约。
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
- [x] 增加队列丢帧策略。
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

## 8. NPU 算子级 profiling（P0，后续算子优化的入口）

> 原则：不根据 ONNX 节点数量猜热点。先用真实 310B、真实输入和固定频率采集算子时间，再决定改图、改精度或写自定义算子。优化结果同时看端到端延迟、精度和稳定性，不能只看单算子耗时。

- [x] 为姿态、人脸、表情三个 OM 分别建立 `msprof` 单模型基线。
  - 每个模型固定 warmup、输入、运行次数和环境温度，至少输出算子级平均/P50/P95。
  - 采集范围：ACL API、Runtime task、GE graph、AICore/AIVector/AICPU、H2D/D2H、内存带宽和任务间空洞。
  - 产物目录建议：`artifacts/profiling/<date>/<model>/<variant>/`，不要提交体积较大的原始 trace，只提交汇总 CSV/Markdown 和复现命令。
- [x] 新增 `scripts/benchmark/profile_om.sh`，把 `msprof` 启动、导出和元数据记录标准化。
  - 自动记录 CANN、驱动、固件、SoC、ATC、Python ACL 版本，OM checksum、输入 shape/dtype/layout 和关键环境变量。
  - 先在板端通过 `msprof --help`/本机 CANN 文档确认参数；CANN 版本不同，不在脚本里硬编码未经验证的参数组合。
- [x] 新增算子汇总脚本，将 profiler 输出聚合为以下视图。
  - Top-K：总耗时、单次耗时、调用次数。
  - 执行单元：AI Core、AI CPU、CPU fallback。
  - 算子族：Conv/MatMul、激活、Resize、Transpose/TransData、Concat/Split、Cast、后处理。
  - 图间隙：Host enqueue、stream synchronize、模型切换和 memcpy 空洞。
- [ ] 导出 ATC/GE 编译日志和可读图，建立 `ONNX node -> GE op -> device task` 映射。
  - 对每个热点保存原始节点名、融合后节点名、输入输出 shape/dtype、执行引擎和耗时。
  - 明确“耗时来自计算、内存带宽、格式转换、同步还是 AICPU fallback”。
- [ ] 建立算子优化准入线。
  - 单算子/算子族占模型执行时间不足 5%，且端到端收益预计不足 1 ms 时，默认不投入自定义算子开发。
  - 任一候选必须有基线、假设、A/B 数据、数值误差和业务准确率结论。

## 9. 图编译、算子融合与混合精度（P0/P1）

- [x] 检查三个 OM 是否存在 AICPU/Host fallback；发现后优先消除。
  - 优先级高于微调普通 AI Core 算子，因为 fallback 往往伴随同步和数据搬运。
  - 处理顺序：替换 ONNX 表达、固定 shape、调整 opset、使用 CANN 支持算子，最后才评估自定义算子。
- [ ] 清点冗余格式转换和碎片算子。
  - 重点搜索 `TransData/Transpose/Cast/Reshape/Squeeze/Unsqueeze/Slice/Concat` 链，以及连续 layout 往返。
  - 统一模型输入输出的 layout/dtype，避免 CPU NCHW 与内部 NZ/NHWC 之间反复转换。
  - 对只改变 view、但被编译成真实搬运的节点做 ONNX 常量折叠或图改写。
- [ ] 验证 Conv + BN + activation 的实际融合情况。
  - 分别检查 YOLO pose、YuNet、MobileNetV3 中 BN 是否已折叠，SiLU/Hardswish/Hardsigmoid 是否形成高效融合算子。
  - 若未融合，比较“导出前模型融合”“ONNX 图融合”“ATC 融合配置”三种路径，保留最稳定方案。
- [ ] 建立 ATC 编译策略矩阵，而不是只保留一个默认 OM。
  - 维度：默认精度、FP16 优先、必须保留 FP32 的敏感节点、融合开关、不同静态输入尺寸。
  - `precision_mode`/`precision_mode_v2`、算子实现选择和 fusion 配置名称以板端 CANN 7.0 实际 `atc --help` 为准。
  - 每个 OM 保存完整 ATC 命令、编译日志、checksum 和输入输出契约。
- [ ] 做 FP16/FP32 混合精度逐模型 A/B。
  - 姿态重点检查小目标、关键点置信度和动作阈值边界。
  - 人脸重点检查低置信度 bbox、`exp` 解码前的回归输出。
  - 表情重点检查相近类别 logits 排序，不只比较 top-1。
  - 验收：输出误差受控，动作/表情回归不退化，并且 `acl_execute_ms` 有实测收益。
- [ ] 检查常量折叠、死分支删除和静态 shape 传播。
  - 所有生产 OM 固定 batch=1 和固定分辨率；删除训练态、动态 shape 和未消费输出。
  - 使用 ONNX checker/shape inference 和板端输出对齐测试共同验收，不能只以 ATC 转换成功为准。
- [ ] 评估融合配置的收益和副作用。
  - 对 profiler 中连续的小算子链尝试 GE 融合；记录融合前后 task 数、workspace、执行时间和精度。
  - 若融合增加 workspace、引入额外 TransData 或没有端到端收益，则回退。
- [ ] 仅在上述路径无法解决明确热点时评估 Ascend C 自定义算子。
  - 候选应满足：稳定 Top-K 热点、现有实现落到 AICPU/多次小 kernel、输入 shape 固定、预计收益覆盖维护成本。
  - 自定义算子必须包含 tiling、边界 shape、精度、性能和 ATC 集成测试；当前不把“写自定义算子”本身作为目标。

## 10. 三个模型的算子/模型图专项（P0/P1）

### 10.1 YOLO Pose

- [ ] 对 `yolo26s-pose` 和 `yolo26n-pose` 生成逐层热点对比。
  - 确认 n 模型 35.8% ACL 收益具体来自哪些 Conv/特征融合/检测头节点，避免只保留模型级结论。
- [ ] 完成 `yolo26n-pose-512.onnx -> OM` 转换和三维 A/B：`s640 / n640 / n512`。
  - 指标：算子时间、模型时间、整帧 P95、三类动作召回率、误触发率和关键点误差。
- [ ] 检查 pose 检测头是否仍有可裁剪的类别/分支。
  - 当前业务只有 person + 17 个关键点；确认导出图不存在未使用类别、冗余输出或 CPU 才会丢弃的分支。
- [ ] 检查 pose 输出是已解码 `[1,300,57]` 还是原始多尺度 head，并记录 NMS/TopK 所在位置。
  - 若 NMS/TopK 在图内且成为 AICPU 热点，比较图外向量化后处理。
  - 若后处理继续保持约 0.3 ms，则停止优化，不为“全 NPU”牺牲可维护性。
- [ ] 针对 profiler 热点验证 SiLU、Concat、Resize/上采样和检测头小卷积的融合/实现选择。
  - 特别关注大量短 kernel 和 TransData 是否导致调度开销高于计算本身。
- [ ] 评估结构化通道剪枝和蒸馏。
  - 以 n640 为 teacher/student 基线，优先剪高耗时 stage，而不是按参数量平均剪枝。
  - 剪枝后重新导出静态 ONNX/OM，并完成动作级回归。

### 10.2 YuNet 人脸检测

- [ ] 优先评估 `640 -> 320` 或 ROI 输入版本。
  - 原始 OpenCV fallback 使用 320 输入，而生产 OM 当前为 640；这是比微调单算子更高潜力的计算量削减项。
  - 验收必须覆盖远距离、小脸、侧脸和遮挡，不能只测镜头前单人。
- [ ] 裁剪未使用的关键点输出分支。
  - 当前业务只消费 bbox/score，`_decode_yunet()` 计算了 5 点但随即丢弃；评估从 ONNX 中删除 `kps_8/16/32` head。
  - 比较 OM 大小、NPU task、12 路输出 D2H、decode 时间和人脸检测一致性。
- [ ] 合并或打包 YuNet 多输出 head。
  - 当前 12 个输出分别 D2H；评估在图末尾 reshape/concat 为较少输出，降低 ACL dataset 和 memcpy 调用开销。
  - 只有 profiler 证明多输出调度/搬运显著时实施，避免为了少量数据增加 TransData/Concat 成本。
- [ ] 向量化 CPU decode，删除无用 landmark 列表构造。
  - 先记录 decode/NMS P95；若不足 1 ms，只修正无用工作，不进行复杂 NPU 后处理迁移。
- [ ] 评估复用 pose 面部关键点直接产生表情 ROI。
  - 高置信度正脸优先使用 pose ROI，低置信度/侧脸/丢失时再运行 YuNet。
  - 这是跨模型算子消除：目标是减少 YuNet 调用次数，而非让两个模型每次都更快。

### 10.3 MobileNetV3 表情模型

- [ ] 从 profiler 确认 Hardswish/Hardsigmoid、SE、GlobalAveragePool、Gemm 是否正确落在 AI Core 并完成融合。
- [ ] 生成 FP16 混合精度 OM，重点保护最后 classifier 和对类别排序敏感的节点。
- [ ] 评估 `112 -> 96` 静态输入和宽度系数更小的模型。
  - 结合蒸馏和现有 7 帧概率平滑，比较 6 类准确率、混淆矩阵和端到端收益。
- [ ] 评估把 softmax 留在 CPU 还是并入图。
  - 当前仅 6 个 logits，CPU softmax 理论成本很小；只有它造成同步/接口复杂度时才改图。
- [ ] 修复并测试 OM `uint8` 输入兼容分支。
  - `expression_infer.py::_preprocess_om()` 的 uint8 分支在 `rgb` 定义前引用它；带 AIPP 的 uint8 OM 会触发该问题。
  - 新增 float32 NCHW、uint8 NCHW、uint8 NHWC 三种输入契约单测。

## 11. ACL Runtime、内存与多模型调度（P0/P1）

- [ ] 把 ACL device/context 生命周期提升为共享 runtime owner。
  - 当前三个 `AclOmSession` 分别执行 `acl.init/set_device/create_context`，且每次 `execute()` 都 `set_context`。
  - 目标：同一 device 共享 context，session 只管理 model/dataset/buffer；验证初始化、切模和退出顺序。
- [ ] 量化 `set_context`、同步 execute 和三模型切换的 Host 开销。
  - 若 `set_context` 可观，减少逐调用设置；若不可观，保留安全路径。
- [ ] 增加 `execute_async + stream/event` 的实验实现。
  - 为姿态和表情/人脸使用独立 stream，先验证 310B 是否能真实重叠，而非只把阻塞转移到最后一次 synchronize。
  - 记录并发前后 AI Core 利用率、内存带宽、单模型 P95 和整帧 P95；资源争用导致长尾变差时回退。
- [ ] 为异步执行实现双缓冲或环形 buffer。
  - 输入、device output、pinned host output 和 NumPy view 必须按 in-flight slot 隔离。
  - 明确 output view 生命周期，禁止下一次 execute 覆盖仍在后处理的数据。
- [ ] 评估只拷贝业务需要的输出。
  - Pose 只需要主人的 bbox/keypoints；YuNet 可删除 landmark 输出；表情只需 6 logits。
  - 优先在模型图裁剪输出，再考虑分段 D2H。
- [ ] 评估 device-resident 数据链路。
  - `DVPP decode/resize -> AIPP/model input -> device output` 尽量避免回到 Host。
  - 人脸 ROI 到表情输入若要留在 device，需验证 DVPP ROI/crop 与模型归一化契约；不要为零拷贝引入更多同步。
- [ ] 增加 ACL 错误码、资源和内存观测。
  - 初始化失败时完整回滚已分配资源；退出检查 model、dataset、buffer、stream、event、context、device 的逆序释放。
  - 长稳测试采集 device/host memory，确认无逐帧增长。
- [ ] 评估模型常驻内存和 workspace 峰值。
  - 三模型并发前先确认 310B 可用内存；记录加载前后占用和峰值，避免异步双缓冲造成 OOM。

## 12. AIPP、DVPP 与数据布局（P1）

- [ ] 为三个模型补齐经过验证的 AIPP 配置，而不只是转换脚本入口。
  - 明确输入格式、通道顺序、量化范围、mean/min/var、crop/resize 能力和最终 tensor dtype/layout。
  - 每份配置配套 100 帧 CPU preprocess vs AIPP 输出/模型输出对齐报告。
- [ ] 优先为 pose 和 expression 生成 uint8 输入 OM。
  - 目标是消除 Host float32 扩张：pose 640 输入从约 1.2 MB uint8 变为约 4.9 MB float32 的开销应由 AIPP 接管。
  - 重新测 H2D、AIPP、execute 和端到端，而不是只看 H2D 字节数。
- [ ] 评估 YuNet letterbox/resize 是否由 AIPP 完成。
  - 校验 padding 颜色、缩放取整和 bbox 反变换必须与现路径一致。
- [ ] 接入 DVPP 解码/颜色转换前，先固定 PYNQ 输出格式契约。
  - 比较当前 UDP/OpenCV BGR、PYNQ NV12 和 DVPP 可直接消费格式，避免 NV12↔BGR↔RGB 往返。
- [ ] 做 DVPP ROI crop -> expression AIPP 的小型 PoC。
  - 目标：只传 ROI 描述/设备地址，不在 ARM CPU 上执行 `cv2.resize/blobFromImage`。
- [ ] 检查宽高 stride/alignment 和零拷贝边界。
  - 将对齐信息纳入帧 metadata，避免每帧重新打包；错误 stride 必须 fail-fast。

## 13. 主流水线、CPU 与系统调优（P0/P1）

- [ ] 将采集、姿态、表情支路和事件发送改为“latest frame”流水线。
  - 每级队列默认深度 1，覆盖旧帧；记录 drop count、queue age 和 in-flight frame id。
  - 端到端低延迟优先，不允许为了提高处理 FPS 累积旧帧。
- [ ] 让表情分支异步消费最近一次 pose/face 信息，并给共享结果增加版本号。
- [x] 以单调时钟补齐每帧 stage trace。
  - 覆盖 capture/assembly、preprocess、H2D、execute、D2H、postprocess、queue、backend 和 player。
  - 跨设备继续保留同步后的 wall clock；单机阶段耗时使用 monotonic/perf counter。
- [ ] 处理摄像头 10 FPS 上限。
  - 测试 USB 摄像头支持的分辨率/FourCC/FPS 组合；正式 NPU 吞吐压测改用可重复视频或内存帧源。
  - 区分“推理吞吐”“实时采集 FPS”和“交互端到端延迟”，不再用摄像头上限掩盖 NPU 收益。
- [ ] 固定和记录 CPU 执行环境。
  - OpenCV/BLAS 线程数、Python 线程、CPU affinity、governor、NPU 频率/温度和降频事件均纳入基线元数据。
  - 分析 ARM CPU 预处理是否与 ACL runtime 线程争抢同一核心。
- [ ] 优化 pose CPU 后处理和动作判断的数据访问。
  - 避免为所有人构造完整 Python 对象后再选择主目标；先按置信度/面积筛主目标，再处理关键点。
  - 只有 profiling 显示其显著时再引入 C++/NEON 扩展。
- [x] 将 JSONL 日志改为独立写线程/批量 flush，并量化关闭日志时的差值。
- [ ] 增加 30 分钟和 4 小时稳定性测试。
  - 指标：P50/P95/P99、丢帧、队列年龄、重连、内存、温度、降频和资源释放。

## 14. 通信、后端与播放器低延迟（P1）

- [ ] 先优化现有 TCP，再决定是否引入 ZeroMQ。
  - 310B sender 设置 `TCP_NODELAY`、keepalive 和可配置重连退避。
  - 区分事件消息与状态消息：动作事件短期可靠，表情状态只保留最新一条。
  - 队列由 30 条改为按类型限深，增加 TTL；重连后不补发已经过期的互动事件。
- [x] 给 `TCPSender` 增加 queue age、drop、reconnect、send duration 指标。
- [ ] 若 A/B 证明 raw TCP 的维护/扇出成为瓶颈，再做 ZeroMQ PoC。
  - 候选：PUSH/PULL 或 DEALER/ROUTER；状态类设置低 HWM/只保留最新值，退出设置零 linger。
  - 验收比较 P50/P95/P99、断线行为、旧消息比例和 CPU，不以“换了中间件”作为完成标准。
- [ ] 后端消除 TCP thread -> asyncio loop -> WebSocket 中不必要的排队。
  - 记录 `backend_receive_ts` 到实际 broadcast/send 完成的时间，而不只记录 queued 时间。
  - 为慢 WebSocket 客户端设置独立限深/覆盖策略，不能拖慢全部客户端。
- [ ] 将 FastAPI `on_event` 迁移到 lifespan，统一启动和关闭 TCP 线程、socket、logger。
- [ ] 播放器 `_cmd_queue` 增加上限、TTL、按场景去重和旧命令覆盖；记录 receive -> actual switch 完成延迟。
- [ ] 视频展示链路比较 MJPEG、低延迟 H.264/WebRTC 或本地直连方案。
  - 当前 MJPEG 编码、HTTP 传输和浏览器解码的 CPU/带宽成本需单独 profiling；不与模型推理指标混算。

## 15. 语音与多模态链路（P1/P2）

- [ ] 为 VAD/ASR/LLM/TTS 增加请求级分段指标和 trace id。
  - 指标：开口检测、尾静音、ASR 首字/终字、LLM 首 token/结束、TTS 首包/首声、播放结束。
- [ ] 降低 ASR 音频 chunk（当前默认 100 ms）并做网络/CPU A/B。
- [ ] 将固定 1.8 秒尾静音改为自适应 endpointing，区分短指令和自然对话。
- [ ] ASR WebSocket 长连接复用，避免每轮重新握手；补心跳、超时、取消和重连状态机。
- [ ] LLM 改为流式输出，按完整短句/标点增量触发 TTS；TTS 改为流式首包播放。
- [ ] 把同步 `ASR -> LLM -> TTS` 改为可取消的后台 pipeline，支持 generation id 和插话打断。
- [ ] 音频队列增加上限和 backpressure；取消后清理旧音频块，避免旧回答恢复播放。
- [ ] 补 ASR/TTS 重试、熔断、错误码分类和降级策略。
- [ ] 评估把本地 VAD、降噪或轻量 ASR 部署为 OM。
  - 先确认云网络是否为主瓶颈；若上 NPU，沿用本文件的算子 profiling、混合精度和准确率流程。
- [ ] 打通视觉事件与语音对话 trace，避免动作视频播放、TTS 和麦克风监听互相阻塞。

## 16. 正确性、可复现性与交付门禁（P0）

- [x] 新增 ONNX/OM 契约检查工具。
  - 校验模型 checksum、输入名/shape/dtype/layout、输出数/shape/dtype，以及 wrapper 预期。
  - 启动时发现契约不匹配应 fail-fast，不能靠 buffer size 猜 dtype 后继续运行。
- [ ] 建立 ONNX Runtime/参考框架 vs OM 的逐层或关键节点对齐工具。
  - 覆盖随机输入、真实帧、边界输入和至少 100 帧数据集。
- [ ] 为每个候选 OM 保存 golden set。
  - Pose：bbox、17 点、confidence；Face：bbox/score；Expression：6 logits/probabilities。
- [ ] 把性能回归加入自动化门禁。
  - 板端环境允许时比较 P50/P95；无板环境至少运行契约、预处理和后处理测试。
  - 性能阈值要允许温度噪声，但明显回退必须阻止替换默认 OM。
- [ ] 完成真实互动验收。
  - `WAVE/HEART/CHEER` 每类至少 10 次；6 类表情每类至少 10 次；记录混淆、漏检和误触发。
  - 覆盖多人、距离、侧身、遮挡、弱光、逆光和连续触发。
- [ ] 完成异常恢复验收。
  - USB 拔插、UDP 丢包/乱序、后端重启、PC 断连、ASR/TTS 超时、SIGINT 和重复启动。
- [ ] 每个完成项在本文追加“环境、命令、数据、结论、回退开关”；没有数据的任务不能勾选。

## 建议实施顺序

1. **P0-证据**：完成真实 310B 的 `msprof` 算子基线、ATC/GE 图映射和准确率基线。
2. **P0-高收益模型项**：完成 `yolo26n-pose` 动作回归、n512 OM；评估 YuNet 320 输入、裁剪未用 keypoint head。
3. **P0-Runtime**：共享 ACL context，补全 stage trace 和 latest-frame 队列，修复 expression uint8/AIPP 输入分支。
4. **P1-编译优化**：消除 AICPU fallback/TransData 热点，验证算子融合与混合精度策略矩阵。
5. **P1-设备数据链**：落地 AIPP uint8 输入，再根据收益评估 DVPP ROI 和异步 stream/双缓冲。
6. **P1-端到端**：优化 TCP/后端/player 队列与 TTL，流式化 ASR/LLM/TTS。
7. **P2-专项研发**：仅针对 profiler 证明的稳定热点进行结构化剪枝、蒸馏或 Ascend C 自定义算子。

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

## 2026-07-17 第四轮实测：海康 GigE、共享 ACL runtime 与算子证据

输入切换为海康 `MV-CS060-10GC` GigE 工业相机（`192.168.1.200`），由 310B `eth1` 直接通过 MVS SDK 取流，不经过 PYNQ。300 帧基线和优化版均启用 overlay、AR、ACL profile 及 640×360@6 FPS/Q65 MJPEG；统计排除前 30 帧。

本轮完成：

- 三个 OM 共享一个 ACL device context；同时加载时 runtime 引用计数为 3，最后统一 destroy/reset/finalize。
- 海康采集使用深度 1 latest-frame；后台复制原始 RGB，实际消费时才转 BGR。相机默认限制为 12 FPS，记录覆盖数、queue/capture age、frame id 和丢包。
- 修复 expression uint8/AIPP 输入分支的未定义 `rgb`，新增 NCHW/NHWC uint8 测试。
- 增加 preprocess/H2D/execute/D2H/postprocess 和 monotonic stage trace。
- JSONL 改为有界异步写线程；2000 条热路径由 36.895 us/条降至 13.924 us/条（-62.3%），关闭时完整 drain。
- TCP 增加 queue age、overflow/expired drop、reconnect、send duration；回环测试 3/3 顺序送达、错误和丢弃均为 0。
- 新增 `audit_om_contracts.py`、`benchmark_om.py`、`profile_om.sh`、`summarize_msprof.py`。

整链路 A/B：

| 指标 | 同步采集基线 | latest-frame + 12 FPS | 变化 |
|---|---:|---:|---:|
| pose_ms 平均 | 49.279 ms | 48.619 ms | -1.3% |
| expression branch 平均 | 22.724 ms | 21.317 ms | -6.2% |
| frame_total_ms 平均 | 59.448 ms | 58.607 ms | -1.4% |
| frame_total_ms P95 | 88.910 ms | 86.708 ms | -2.5% |
| 处理 FPS 字段平均 | 12.180 | 12.435 | +2.1% |

优化版相机采集 309 帧、交付 300 帧、覆盖旧帧 8、timeout 0、GigE lost packet 0；capture-to-pipeline 平均 77.478 ms、P95 123.188 ms。完整记录位于 `artifacts/optimized/20260717_hik_gige_300_v3/`。

`msprof` 固定输入单模型结果：Pose s640 ACL execute 32.896 ms，设备 task 30.703 ms，首要热点 `GatherElements` 17.57%；YuNet 640 为 7.532/6.298 ms，首层 Conv2D 20.39%；expression 为 1.346/0.900 ms，首要热点 MatMulV2 10.91%。三模型 AICPU calls 均为 0。原始 trace 和 Top-K 位于 `artifacts/profiling/20260717/`。

限制：现场图像很暗且失焦，画面中没有可用于统计的人脸或动作。本轮不宣称三类动作或六类表情准确率达标；人工调焦、补光、真人入镜并完成每类至少 10 次回归前，不替换默认姿态 OM。
