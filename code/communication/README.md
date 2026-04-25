# UDP 视频流通信说明

本目录当前用于三端 UDP 视频流联调：`PYNQ -> 310B -> PC`，并保留 `PC -> 310B -> PYNQ` 控制透传通道。

## 当前状态

- ✅ **视频分片协议**: 已完成三端编译和基础联调
- ✅ **上行链路**: PYNQ 采集帧并分片发送到 310B，310B 中继转发到 PC
- ✅ **下行链路**: PC 发送控制消息到 310B，310B 透传到 PYNQ
- 🔴 **推理数据通道**: 待集成（310B 推理结果→PC）
- 🔴 **IP 视频触发通道**: 待集成（PC 根据推理结果触发视频播放）

## 当前链路

### 已实现

- **上行视频**: PYNQ 采集帧（RGB24 或 MJPEG）并分片发送到 310B
- **中继转发**: 310B 原样转发视频分片到 PC，不做重组和推理
- **落盘编码**: PC 重组完整帧后调用 ffmpeg 编码输出 MP4
- **下行控制**: PC 发送控制消息到 310B，310B 透传到最近活跃的 PYNQ 源地址

### 待扩展

- **推理数据上行**: 310B 将姿态/表情推理结果通过独立端口发送至 PC
- **IP 视频触发下行**: PC 根据推理结果触发对应 IP 视频播放，并反馈状态

## 文件说明

- [include/udp_common.h](include/udp_common.h) 提供工程共通定义：IP/端口、视频分片协议头、字节序转换、UDP 收发和 socket 缓冲区调优函数
- [src/pynq_udp.cpp](src/pynq_udp.cpp) 是 PYNQ 端采集与分片发送程序（V4L2 mmap）
- [src/310B_udp.cpp](src/310B_udp.cpp) 是 310B 端纯转发与控制透传程序
- [src/pc_udp.cpp](src/pc_udp.cpp) 是 PC 端分片重组和 MP4 落盘程序
- [CMakeLists.txt](CMakeLists.txt) 提供 CMake 构建入口

## 协议与共通接口

### 消息类型

- `MsgType::VIDEO_FRAGMENT`: 视频分片（当前已实现）
- `MsgType::INFERENCE_RESULT`: 推理结果（待实现，310B→PC）
- `MsgType::IP_VIDEO_TRIGGER`: IP 视频触发指令（待实现，PC→310B/PYNQ）

### 视频分片协议

- 像素格式：`PIX_FMT_RGB24`、`PIX_FMT_MJPEG`
- 分片包头：`VideoPacketHeader`，核心字段包含 `frame_id`、`frag_idx`、`frag_cnt`、`payload_bytes`、`width`、`height`、`pixel_format`、`timestamp_us`

### 共通函数

- `host_to_net` / `net_to_host`: 包头字节序转换
- `parse_video_header`: 视频分片头解析和校验
- `send_bytes` / `recv_bytes`: 二进制收发
- `tune_socket_buffers`: 设置 UDP 缓冲区，降低突发丢包概率

## 310B 接口说明

### 1) 上行视频接口（PYNQ -> 310B -> PC）

- 310B 监听：`ASCEND_310B_IP:PYNQ_PORT`
- 输入：PYNQ 发送的视频分片 UDP 包（二进制，带 `VideoPacketHeader`）
- 输出：310B 按原包转发到 `PC_IP:PC_PORT`
- 行为：
  - 仅做中继，不做解码、不做重组、不做推理
  - 每收到一个上行视频包就尝试转发一次
  - 日志关键字：`[310B] forward video frame=... frag=.../...`

### 2) 下行控制接口（PC -> 310B -> PYNQ）

- 310B 监听：`ASCEND_310B_PC_IP:PC_PORT`
- 输入：PC 发送的控制消息 UDP 包（可用文本或二进制）
- 输出：透传到最近一次上行视频包对应的 PYNQ 源地址
- 行为：
  - 当且仅当 310B 已学习到 PYNQ 源地址后，控制消息才会下发
  - 日志关键字：`[310B] forward control to PYNQ size=...`

### 3) 推理数据上行（待实现）

- 310B 主动发送：推理结果（JSON 格式）到 `PC_IP:INFERENCE_PORT`
- 数据内容：姿态关键点、动作标志、表情分类结果
- 触发时机：每帧推理完成后立即发送

## 已知地址

| 设备 | 接口 | IP 地址 | 用途 |
|------|------|---------|------|
| PC | eth0 | 192.168.137.1 | 与 310B 通信 |
| 310B | eth0 | 192.168.2.2 | 与 PYNQ 通信 |
| 310B | eth1 | 192.168.137.100 | 与 PC 通信 |
| PYNQ | eth0 | 192.168.2.99 | 与 310B 通信 |

### 端口分配

| 端口 | 用途 | 方向 |
|------|------|------|
| 5555 | PC 监听端口 (PC_PORT) | 310B→PC 视频转发 |
| 9090 | PYNQ 监听端口 (PYNQ_PORT) | 310B→PYNQ 控制透传 |
| TBD | 推理数据端口 (INFERENCE_PORT) | 310B→PC 推理结果 |
| TBD | IP 视频触发端口 | PC→310B/PYNQ 视频触发 |

## 构建步骤

在通信目录下执行：

```bash
cmake -S . -B build
cmake --build build -j
```

生成目标：

- `build/pc_udp`
- `build/310B_udp`
- `build/pynq_udp`

依赖说明：

- PC 端需要已安装 `ffmpeg`（用于生成 `output.mp4`）

## 运行顺序

1. 先启动 `pc_udp`：`./build/pc_udp`
2. 再启动 `310B_udp`：`./build/310B_udp`
3. 最后启动 `pynq_udp`：`./build/pynq_udp`

## 成功判定

- PYNQ 端持续看到 `send frame`，且 `frag_cnt` 与 `bytes` 合理
- 310B 端持续看到 `forward video frame`
- PC 端出现 `write frame ok=...`，并生成可播放的 `output.mp4`

参考日志关键字：

- PYNQ: `[PYNQ] camera ready`、`[PYNQ] send frame=... bytes=... fmt=...`
- 310B: `[310B] start relay loop`、`[310B] forward video frame=...`
- PC: `[PC] start ffmpeg`、`[PC] write frame ok=...`、`[PC] stop, ok_frame=...`

## 执行清单

1. 确认 [include/udp_common.h](include/udp_common.h) 中 IP 和端口已经填写
2. 在 [CMakeLists.txt](CMakeLists.txt) 目录执行 CMake 构建
3. 依次启动 PC、310B、PYNQ 三个程序
4. 观察三端日志中是否出现持续视频转发和 `write frame ok`
5. 结束时先停 PYNQ，再停 310B，最后停 PC，确保 MP4 正常收尾
6. 如需排错，先看端口是否被占用，再看 IP 是否可达和摄像头格式是否匹配

## 下一步扩展计划

1. **新增推理数据通道**
   - 在 310B 端添加推理结果发送逻辑
   - PC 端添加推理结果接收和解析模块
   - 定义推理结果 JSON 协议格式

2. **新增 IP 视频触发通道**
   - PC 端根据推理结果匹配预生成视频
   - 发送视频触发指令到 310B/PYNQ
   - 实现视频播放状态反馈

3. **性能优化**
   - 端到端时延测试（目标 < 100ms）
   - UDP 丢包率监测和重传机制
   - 多路并发压力测试