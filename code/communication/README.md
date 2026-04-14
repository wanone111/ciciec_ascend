# UDP 视频流通信说明（PYNQ <-> PC）

本目录当前阶段用于验证 PYNQ 到 PC 的 UDP 视频流传输，并保留低频 ACK 控制回路。

## 当前状态

- 已完成 PYNQ 与 PC 的 UDP 基础互通验证。
- 已切换为 NV12 视频流传输模式（1280x960@60fps）。
- 接收端执行整帧完整性校验：缺包即丢整帧，不做重传。

## 当前联通关系

- PYNQ 直接从 PL 预处理后的 DMA/CMA 连续 NV12 缓冲读取帧数据并发送分片流到 PC。
- PC 持续接收并重组分片，只有完整帧才进入显示/统计流程。
- PC 每 30 帧回发一次 ACK，PYNQ 接收 ACK 仅用于链路健康观测。

## 文件说明

- [include/udp_common.h](include/udp_common.h) 提供 UDP 公共函数、IP 宏和端口常量。
- [src/client_udp.cpp](src/client_udp.cpp) 是 PC 端程序，负责分片重组、整帧校验、可选显示与周期 ACK。
- [src/pynq_udp.cpp](src/pynq_udp.cpp) 是 PYNQ 端程序，负责从 DMA 缓冲读取 NV12 帧、分片发送与周期 ACK 接收。
- [CMakeLists.txt](CMakeLists.txt) 提供 CMake 构建入口。

## 参数配置

请在 [include/udp_common.h](include/udp_common.h) 中确认以下宏值已经填写正确：

- `PC_IP`
- `PYNQ_IP`
- `PC_PORT`
- `PYNQ_PORT`

视频参数（定义在 `udp_common.h`）：

- `VIDEO_WIDTH = 1280`
- `VIDEO_HEIGHT = 960`
- `VIDEO_FPS = 60`
- `UDP_PAYLOAD_BYTES = 1200`
- `ACK_EVERY_N_FRAMES = 30`

说明：

- PC 程序监听 `PC_PORT`。
- PYNQ 程序监听 `PYNQ_PORT`。
- PYNQ 发送目标是 `PC_IP:PC_PORT`。
- 每个 UDP 包格式为 `UdpHeader + payload`，其中 `seq_num/total` 用于分片重组。

## 已验证网络参数

- `PC_IP = 192.168.2.100`
- `PYNQ_IP = 192.168.2.99`
- `PC_PORT = 5555`
- `PYNQ_PORT = 9090`

说明：

- PYNQ 地址固定为 `192.168.2.99`。
- PC 与 PYNQ 需处于同一网段（`192.168.2.x`）。

## 构建步骤（CMake）

依赖：

- PYNQ 端：不依赖 OpenCV。
- PC 端：默认不依赖 OpenCV（仅日志模式）；如需窗口显示可显式开启。

在通信目录下执行：

```bash
cmake -S . -B build
cmake --build build -j
```

若需要启用 PC 端窗口显示（要求 OpenCV 可用且 ABI 兼容），使用：

```bash
cmake -S . -B build -DENABLE_OPENCV_DISPLAY=ON
cmake --build build -j
```

生成目标：

- `build/pc_udp`
- `build/pynq_udp`

## 运行顺序

1. 先在 PC 上运行 `./build/pc_udp`。
2. 再在 PYNQ 上运行 `./build/pynq_udp`。
3. 两端持续运行，观察帧重组和 ACK 日志是否稳定。

可选显示：

- 若希望 PC 显示视频窗口，可在 PC 端启动前设置环境变量：`export UDP_SHOW=1`。
- 若构建时未找到 OpenCV，`UDP_SHOW=1` 会自动降级为仅日志模式。
- 若不设置，PC 端默认只打印日志与统计信息。

PYNQ DMA 输入：

- 发送端默认读取设备节点 `/dev/udmabuf0`。
- 如需切换 DMA 设备路径，可设置环境变量：`export UDP_DMA_DEV=/dev/udmabufX`。
- DMA 缓冲要求：连续内存、无 stride 对齐补齐、至少包含一帧 NV12（`1280*960*3/2` 字节）。

## 成功判定

当双向通信成功时，日志应持续出现：

- PYNQ 端持续打印已发送帧统计日志。
- PC 端持续打印完整帧统计日志（`complete` 递增）。
- PC 端按 30 帧间隔打印 `send ACK ok`。
- PYNQ 端按 30 帧间隔打印 `recv ACK` 或超时日志。

补充：

- 手动按 `Ctrl+C` 结束程序时，终端返回 `Exit Code: 130` 属于正常退出现象。

## 协议行为

- 像素格式：NV12。
- 传输策略：UDP 应用层分片。
- 丢包策略：只显示完整帧，缺包丢整帧。
- 重传策略：不重传（低延迟优先）。
- ACK 策略：每 30 帧 ACK 一次。

说明：

- 当前头结构沿用 `frame_id/seq_num/total/payload_len/timestamp`。
- 若后续发现跨端兼容或扩展不足，再在文档中补充 `magic/version` 等字段。

## 常见问题

- 若 PYNQ 端 DMA 打开失败，请先检查 `UDP_DMA_DEV` 指向的设备节点是否存在并有读权限。
- 若 PYNQ 端提示 DMA 缓冲区过小，请检查 DMA 分配大小是否不小于一帧 NV12 字节数。
- 若 PC 端 `drop` 持续上升，通常是网络丢包或接收处理跟不上导致。
- 若两端同网段仍收不到包，请检查防火墙是否放行 UDP `5555/9090`。
- 若 CMake 报 OpenCV 未找到，请先安装 OpenCV 开发包后重新配置构建。

## 快速回归清单

1. 确认 [include/udp_common.h](include/udp_common.h) 中 IP 与端口与现场一致。
2. 在通信目录执行一次 CMake 全量编译。
3. 先启动 `pc_udp`，再启动 `pynq_udp`。
4. 观察 `complete`、`dropped` 与 ACK 日志是否符合预期。
5. 连续运行 10 分钟，确认无持续性异常。
