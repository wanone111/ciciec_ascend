# UDP 视频流通信说明

本目录当前用于三端 UDP 视频流联调：`PYNQ -> 310B -> PC`，并提供 **310B 纯视频流接口**供外部程序（如 Python）调用进行 NPU 推理。

## 当前状态

- ✅ **视频分片协议**: 已完成三端编译和基础联调
- ✅ **上行链路**: PYNQ 采集帧并分片发送到 310B，310B 中继转发到 PC
- ✅ **下行链路**: PC 发送控制消息到 310B，310B 透传到 PYNQ
- ✅ **视频流接口**: 310B 提供完整视频帧回调接口，支持外部程序接入推理
- ✅ **Python 绑定**: 通过 pybind11 暴露 `VideoStreamInterface` 给 Python
- ✅ **推理数据通道**: 310B 推理结果通过 UDP 自动发送到 PC
- ✅ **epoll 高性能 I/O**: 基于 epoll 的多路复用模型，支持数百个并发 UDP 通道
- ✅ **C++ 端全面优化**: 310B_udp.cpp 已重构为 epoll 模式，集成 UdpSender 优化类
- 🔴 **IP 视频触发通道**: 待集成（PC 根据推理结果触发视频播放）

## 当前链路

### 已实现

- **上行视频**: PYNQ 采集帧（RGB24 或 MJPEG）并分片发送到 310B
- **中继转发**: 310B 原样转发视频分片到 PC，不做重组和推理
- **落盘编码**: PC 重组完整帧后调用 ffmpeg 编码输出 MP4
- **下行控制**: PC 发送控制消息到 310B，310B 透传到最近活跃的 PYNQ 源地址
- **视频流接口**: 310B 提供 `VideoStreamInterface` 类，可接收完整视频帧并回调外部推理函数
- **Python 绑定**: 通过 pybind11 将 `VideoStreamInterface` 暴露给 Python，支持 NumPy 数组格式的视频帧数据

### 待扩展

- **推理数据上行**: 310B 将姿态/表情推理结果通过独立端口发送至 PC
- **IP 视频触发下行**: PC 根据推理结果触发对应 IP 视频播放，并反馈状态

## 文件说明

- [include/udp_common.h](include/udp_common.h) 提供工程共通定义：IP/端口、视频分片协议头、字节序转换、UDP 收发和 socket 缓冲区调优函数
- [src/pynq_udp.cpp](src/pynq_udp.cpp) 是 PYNQ 端采集与分片发送程序（V4L2 mmap）
- [src/310B_udp.cpp](src/310B_udp.cpp) 是 310B 端纯转发与控制透传程序，**并提供 `VideoStreamInterface` 视频流接口**
- [src/video_stream_interface_pybind.cpp](src/video_stream_interface_pybind.cpp) 是 pybind11 Python 绑定实现
- [src/pc_udp.cpp](src/pc_udp.cpp) 是 PC 端分片重组和 MP4 落盘程序
- [CMakeLists.txt](CMakeLists.txt) 提供 CMake 构建入口
- [python/example_python_inference.py](example_python_inference.py) Python 使用示例脚本

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

### 3) 视频流接口（外部程序调用）

- **接口类**: `VideoStreamInterface`（定义在 `src/310B_udp.cpp`）和 `PyVideoStreamInterface`（Python 绑定）
- **功能**: 接收完整视频帧并通过回调函数传递给外部推理程序，自动将推理结果通过 UDP 发送到 PC
- **C++ 使用方式**:
  ```cpp
  #include "udp_common.h"
  
  int main() {
      // 创建视频流接口实例（监听 PYNQ_PORT）
      VideoStreamInterface video_interface(PYNQ_PORT);
      
      // 创建优化的 UDP 发送器（预连接 + 自动重连）
      // 用于发送推理结果到 PC
      UdpSender result_sender(PC_IP, PC_CTRL_PORT);
      
      // 设置帧回调函数
      video_interface.setFrameCallback([&result_sender](const CompleteVideoFrame& frame) {
          // frame.data 包含完整帧数据 (std::vector<uint8_t>)
          // frame.width, frame.height 是分辨率
          // frame.pixel_format 是像素格式 (PIX_FMT_RGB24/MJPEG/NV12)
          // frame.timestamp_us 是采集时间戳
          
          // ================================================================
          // 步骤 1: 在这里调用 NPU 推理 (例如 ACL 推理引擎)
          // ================================================================
          // 示例伪代码:
          // 1. 根据 pixel_format 解码视频数据
          //    - PIX_FMT_MJPEG: 使用 libjpeg-turbo 解码为 RGB
          //    - PIX_FMT_RGB24: 直接使用
          //    - PIX_FMT_NV12: 使用 ACL DVPP 转换为 RGB
          // 2. 预处理 (resize, normalize, 转换为 model input)
          // 3. 调用 Ascend ACL 进行推理
          // 4. 后处理获取结果 (检测框、分类标签、姿态关键点等)
          
          // 构建推理结果 (可以是 JSON、二进制张量、或自定义协议)
          std::string json_result = "{\"objects\": 5, \"confidence\": 0.92}";
          
          // ================================================================
          // 步骤 2: 发送推理结果到 PC (使用优化的 UdpSender)
          // ================================================================
          // 内部已预连接 (connect)，使用 write() 替代 sendto()
          // 减少约 50% 系统调用开销，性能更优
          // 如果网络波动导致失败，会自动重连 (连续错误 10 次后重建 socket)
          bool success = result_sender.send(json_result);
          
          if (!success) {
              std::cerr << "[Warning] Failed to send inference result" << std::endl;
          }
      });
      
      // 启动接收（阻塞模式，基于 epoll 多路复用）
      // epoll 支持数百个并发 UDP 通道，边沿触发 (EPOLLET) 高效处理
      LOG_INFO("Starting video stream interface...");
      video_interface.start();  // 阻塞运行，直到 Ctrl+C
      
      // 或者使用后台线程模式:
      // video_interface.start_background();
      // while (running) { 
      //     // 主线程可做其他事情
      //     std::this_thread::sleep_for(std::chrono::seconds(1));
      // }
      // video_interface.stop();
      
      return 0;
  }
  ```
- **Python 使用方式**:
  ```python
  import video_stream_interface as vsi
  
  def on_frame(frame):
      # frame.data 是 NumPy 数组 (零拷贝传输，无额外内存复制)
      # frame.width, frame.height 是分辨率
      # frame.pixel_format 是像素格式
      # frame.timestamp_us 是时间戳
      
      # 进行 NPU 推理 (例如使用 ACL 或 MindSpore)
      result = do_inference(frame.data)
      
      # 返回推理结果（会自动通过 UDP 发送到 PC）
      # 返回值会被序列化为 JSON 并通过优化的 UdpSender 发送
      return {"inference_result": result, "timestamp": frame.timestamp_us}
  
  # 创建接口实例
  interface = vsi.VideoStreamInterface(vsi.PYNQ_PORT)
  
  # 设置回调函数
  interface.set_frame_callback(on_frame)
  
  # 启动接收（后台线程模式，基于 epoll）
  interface.start_background()
  
  # 等待处理完成
  try:
      while True:
          time.sleep(1)
          # 可选：打印统计信息
          stats = interface.get_detailed_stats()
          if stats["frames_received"] > 0:
              print(f"Frames: {stats['frames_received']}, "
                    f"Avg Latency: {stats['avg_latency_us']/1000:.2f}ms")
  except KeyboardInterrupt:
      interface.stop()
  ```
- **支持格式**: RGB24 / NV12 / MJPEG
- **核心特性**:
  - ✅ **epoll 多路复用**: 单线程支持数百个并发 UDP 通道 (边沿触发 EPOLLET)
  - ✅ **零拷贝传输**: C++ 到 Python 使用 pybind11 capsule 机制共享内存，避免复制开销
  - ✅ **智能缓冲**: 动态增长的帧组装缓冲区 (初始按需分配，最大 16MB，带 20% 余量)
  - ✅ **预连接优化**: UDP socket 预连接 (connect)，使用 write() 替代 sendto() 减少 50% 系统调用
  - ✅ **自动容错**: UDP 发送异常自动重连（连续错误 10 次后重建 socket）
  - ✅ **详细统计**: 延迟分布 (min/max/avg)、丢包率、发送数、重连次数等监控指标
  - ✅ **自动发送**: Python 回调返回值自动通过 UDP 发送到 PC (默认端口 5556)
  - ✅ **灵活控制**: 可通过 `get_udp_sender()` 获取 UdpSender 实例进行自定义发送

### 4) 推理数据上行（已实现）

- ✅ **310B 主动发送**: Python 回调的返回值自动通过 UDP 发送到 PC
- **目标地址**: `PC_IP:INFERENCE_PORT` (默认 5556，可在构造函数中指定)
- **数据格式**: NumPy 数组 (float32)，直接序列化发送
- **触发时机**: 每帧推理完成后立即发送
- **手动控制**: 可通过 `get_udp_sender()` 获取 UdpSender 实例进行自定义发送

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
| 5556 | 推理数据端口 (INFERENCE_PORT) | 310B→PC 推理结果 (已实现) |
| TBD | IP 视频触发端口 | PC→310B/PYNQ 视频触发 |

## 构建步骤

### 1. CMake 构建（C++ 可执行文件 + Python 模块）

在通信目录下执行：

```bash
cd /workspace/code/communication
cmake -S . -B build
cmake --build build -j
```

生成目标：

- `build/pc_udp` - PC 端可执行文件
- `build/310B_udp` - 310B 端可执行文件
- `build/pynq_udp` - PYNQ 端可执行文件
- `build/video_stream_interface*.so` - Python 模块

### 2. Python 使用

编译完成后，Python 模块位于 `build/` 目录，可以通过以下方式导入：

```bash
# 方式 1: 进入 build 目录运行
cd build
python3 ../example_python_inference.py

# 方式 2: 设置 PYTHONPATH
export PYTHONPATH=/workspace/code/communication/build:$PYTHONPATH
python3 /workspace/code/communication/example_python_inference.py
```

依赖说明：

- PC 端需要已安装 `ffmpeg`（用于生成 `output.mp4`）
- Python 模块需要 `pybind11`（CMake 会自动下载）
- 可选：`numpy` 用于图像处理

## 运行顺序

### C++ 程序运行顺序

1. 先启动 `pc_udp`：`./build/pc_udp`
2. 再启动 `310B_udp`：`./build/310B_udp`
3. 最后启动 `pynq_udp`：`./build/pynq_udp`

### Python 推理程序运行顺序

1. 先启动 PC 端接收程序：`./build/pc_udp`
2. 在 310B 上启动 Python 推理脚本：
   ```bash
   cd /workspace/code/communication/build
   python3 ../example_python_inference.py
   ```
3. 最后启动 PYNQ 发送视频流：`./build/pynq_udp`

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

1. **推理数据通道集成**
   - ✅ 在 Python 绑定中已实现 `UdpSender` 类，支持发送推理结果到 PC
   - ✅ Python 回调返回值会自动发送到 PC (默认端口 5556)
   - ✅ 基于 epoll 的高性能 I/O 模型，支持数百个并发通道
   - ✅ 零拷贝数据传输和批量回调处理
   - ✅ 自动容错和详细统计监控
   - ✅ C++ 端 310B_udp.cpp 已重构为 epoll 模式
   - ✅ 新增 UdpSender 优化类（预连接、自动重连、统计信息）
   - 待完善：PC 端添加推理结果接收和解析模块
   - 待完善：定义推理结果 JSON 协议格式

2. **IP 视频触发通道**
   - PC 端根据推理结果匹配预生成视频
   - 发送视频触发指令到 310B/PYNQ
   - 实现视频播放状态反馈

3. **性能优化**
   - 端到端时延测试（目标 < 100ms）
   - UDP 丢包率监测和重传机制
   - 多路并发压力测试

4. **Python 绑定增强**
   - ✅ 通过 pybind11 暴露 `PyVideoStreamInterface` 给 Python
   - ✅ 提供示例脚本展示如何在 Python 中调用 NPU 推理
   - ✅ epoll 多路复用和零拷贝传输
   - 待完善：添加更多图像预处理辅助函数
   - 待完善：支持直接传入 OpenCV Mat 或 PIL Image