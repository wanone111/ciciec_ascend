# 双板通信代码（ZeroMQ 视频链路原型）

当前目录用于打通 PYNQ-Z2 到 PC/310B（当前也可先在 PC 上验证）的单向视频链路，通信层采用 ZeroMQ。

## 1. 当前状态（以仓库现状为准）

本目录已具备可构建的 CMake 工程与视频流原型代码，核心能力如下：

- 发送端：采集摄像头帧，JPEG 编码后通过 ZeroMQ PUSH 发送。
- 接收端：ZeroMQ PULL 接收，解析后 JPEG 解码并实时显示。
- 线程模型：主线程采集/显示，后台线程负责网络收发。
- 拥塞策略：队列满时丢弃旧帧，优先保留新帧，降低端到端延迟累积。

当前仍是“链路打通验证版”，未完成项包括：

- 发送端 IP 与端口仍为硬编码（`192.168.2.100:5555`），未参数化。
- 尚未接入方案文档中的 ROI/ISP/控制通道等功能。
- 尚未加入消息校验与更完整协议头版本管理。

## 2. 目录与文件说明

- `include/zmq_streamer.hpp`
  - `FramePacket`：帧号、时间戳与 JPEG 负载。
  - `ZmqVideoSender`：发送端封装（PUSH）。
  - `ZmqVideoReceiver`：接收端封装（PULL）。
  - 当前已添加关键流程注释（线程模型、拥塞策略、协议组包/解包）。
- `src/pynq_zmq_pub.cpp`
  - 发送端示例主程序。
  - 摄像头采集 -> resize -> 时间戳 -> 入发送队列。
- `src/310B_zmq_sub.cpp`
  - 接收端示例主程序。
  - 拉流 -> 解码 -> 延迟估算 -> 画面显示。
- `CMakeLists.txt`
  - 已补齐，支持构建两个可执行文件。

## 3. 数据路径与协议（当前实现）

### 3.1 数据路径

1. 发送端采集摄像头画面（默认 640x640@30fps）。
2. 每帧 JPEG 编码（质量 80）。
3. 组帧后通过 ZeroMQ 发送。
4. 接收端拉取消息并解析。
5. OpenCV 解码显示，并叠加帧号和估计延迟。

### 3.2 单帧消息格式

当前发送格式为：

- `4B`：`frame_id`（`uint32_t`）
- `8B`：`timestamp`（`double`，秒）
- `N B`：JPEG 字节流

即总长度为 `12 + jpeg_size`。

## 4. 通信与性能策略

- 套接字模型：`PUSH -> PULL`
- 非阻塞发送：`dontwait`
- 接收线程非阻塞轮询 + 短睡眠退避
- `sndhwm` / `rcvhwm` 设置为 8
- 队列满丢旧帧，优先“最新帧”

该策略目标是降低实时链路尾延迟，代价是高负载时可能出现可见丢帧。

## 5. 依赖环境

至少需要：

- C++17 编译器（g++/clang++）
- OpenCV4
- ZeroMQ（`libzmq`）及其 C++ 头文件（`cppzmq`，即 `zmq.hpp`）
- pthread
- CMake >= 3.16
- pkg-config

## 6. 构建与运行（推荐 CMake）

```bash
cd code/communication
cmake -S . -B build
cmake --build build -j
```

生成目标：

- `build/pynq_zmq_pub`
- `build/zmq_sub_310b`

运行顺序建议：

1. 在接收端主机启动 `./build/zmq_sub_310b`。
2. 在发送端主机启动 `./build/pynq_zmq_pub`。
3. 确认发送端代码中的目标 IP 与接收端 IP 一致。

## 7. 已知环境问题（本机当前验证）

在 conda 环境 `310B_env` 下，链接阶段可能出现 ABI 不匹配错误（例如 `libhwy` / `libopencv_videoio` 的符号版本问题）。

常见处理方式：

- 使用系统工具链 + 系统 OpenCV 重新配置并构建。
- 或保持 conda 方案，但统一编译器与 OpenCV/`libstdc++` 的 ABI 版本。

## 8. 与总体方案的关系

本目录当前承担“先跑通链路、可视化验证延迟”的阶段目标。

后续若进入完整赛题链路，建议在此基础上继续补齐：

- 可配置 ROI
- 轻量 ISP（白平衡/降噪/gamma）
- 控制通道（参数热更新）
- 正式帧头与协议版本管理

## 9. 注意事项

- 发送端 `sleep_for(33ms)` 与摄像头实际帧率可能不完全一致，可能引入节拍偏差。
- 当前时间戳使用系统时间（`system_clock`），跨设备时钟不同步会影响延迟绝对值。
