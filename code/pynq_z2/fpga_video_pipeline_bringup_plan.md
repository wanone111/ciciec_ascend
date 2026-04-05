# PYNQ-Z2 + Ascend 310B 视频通路打通与分工方案

## 1. 目标与原则

目标：先把视频链路稳定打通，再逐步把算子从软件迁移到 FPGA/HLS，最后对接 310B 推理。

设计原则：

1. 单板只做自己擅长的事情，避免重复计算。
2. 传输层用固定、轻量、可验证的格式，优先保证稳定。
3. 先打通、后优化、再硬化。

## 2. 推荐系统分工（避免重复计算）

### 2.1 PYNQ-Z2（FPGA + ARM）

职责：

1. 视频采集（USB 摄像头）。
2. 低成本预处理：ROI、基础去噪、白平衡、gamma（当前可先 CPU 实现，后迁 HLS）。
3. 输出统一线格式：YUV420 8bit。
4. 通过 ZeroMQ PUB 推流。

不建议在 PYNQ 做的事：

1. 模型归一化/标准化（均值方差等）与 CHW 转换。
2. 与模型强绑定的前处理。
3. 检测后处理（NMS、跟踪等）。

### 2.2 Ascend 310B

职责：

1. 接收帧并完成模型输入转换。
2. 推理执行。
3. 推理后处理与业务逻辑。
4. 输出检测结果（框、类别、分数）和性能指标。

### 2.3 为什么这样分工

如果在 PYNQ 提前做模型输入转换，310B 侧通常还要再做一遍适配，等于重复计算并增加传输复杂度。统一传 YUV420 可以降低带宽、简化调试，并且能兼容不同模型。

## 3. 数据与接口约定

### 3.1 视频流协议

当前工程使用 `code/communication/video_stream.h`，核心字段：

1. frame_id
2. timestamp_ns
3. width/height
4. roi_x/y/w/h（permille）
5. pixel_format（当前固定 YUV420P8）
6. payload_bytes

### 3.2 链路格式

推荐固定为：

1. 输入：BGR
2. 处理后输出：YUV420 planar
3. 网络传输：header + payload（整帧）

后续如需降低时延抖动，再考虑分片传输和零拷贝。

## 4. 视频通路打通路线图

## 阶段 A：最小链路可见（今天完成）

目标：PYNQ 发送，PC/310B 接收并显示。

执行：

1. 编译并运行 `video_pub.cpp`。
2. 编译并运行 `video_sub.cpp`。
3. 验证帧号递增、FPS 稳定、平均延迟可观测。

验收标准：

1. 接收端持续显示画面。
2. 统计日志持续输出 FPS 和延迟。
3. invalid_or_drop 不持续上升。

## 阶段 B：参数在线可控

目标：运行中可调 ROI/gamma/denoise。

执行：

1. 增加 REQ/REP 控制通道（建议端口 5556）。
2. 参数更新在发送端“下一帧”生效。
3. 在帧头回填当前参数，便于端到端核对。

## 阶段 C：HLS 迁移

目标：把 CPU 预处理替换为 HLS IP。

执行：

1. 先迁 ROI + 色彩转换（收益大、结构简单）。
2. 再迁去噪和 gamma（按资源评估）。
3. 保持协议不变，避免上下游联动大改。

## 阶段 D：310B 推理联调

目标：视频链路 + 推理链路闭环。

执行：

1. 310B 侧接收 YUV420 并转换成模型输入。
2. 完成推理与后处理。
3. 统计端到端性能（采集到结果）。

## 5. 当前目录可直接使用的命令

进入 `code/communication` 后：

```bash
g++ -O2 -std=c++17 video_pub.cpp -o video_pub $(pkg-config --cflags --libs opencv4 libzmq)
g++ -O2 -std=c++17 video_sub.cpp -o video_sub $(pkg-config --cflags --libs opencv4 libzmq)
```

接收端先启动：

```bash
./video_sub --connect tcp://<PYNQ_IP>:5555 --show 1 --hwm 2
```

发送端后启动：

```bash
./video_pub --bind tcp://*:5555 --camera 0 --width 640 --height 480 --fps 30 --roi 0,0,1,1 --gamma 2.2 --denoise 3 --show 0
```

## 6. 性能预算参考

在 640x480@30 下建议目标：

1. 单帧处理 + 发送总耗时 <= 33ms（确保 30fps）。
2. 端到端平均延迟 <= 50ms。
3. 丢帧率 <= 2%。

## 7. 你当前最该做的三步

1. 先按阶段 A 跑通画面和日志，不要同时改多个模块。
2. 再引入控制通道，把 ROI/gamma 调参流程固化。
3. 最后开始 HLS 迁移，并保持网络协议不变。

这样可以确保每一步都有可观测结果，出问题也能快速定位。
