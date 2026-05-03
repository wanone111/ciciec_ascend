# 通信模块说明

本目录实现 PYNQ-Z2、Ascend 310B 和 PC 之间的 UDP 视频流通信，并提供 `video_stream_interface` pybind11 模块，供 `code/ascend_310B/smart_ip_interaction/run_pipeline.py` 以 Python 方式接收 PYNQ 视频帧。

## 当前能力

- PYNQ 端通过 V4L2 采集视频帧并按 UDP 分片发送。
- 310B 端可作为视频中继，也可通过 pybind11 接收完整帧给 Python 推理程序使用。
- PC 端可重组视频分片并写入 MP4，适合链路验证。
- Python 绑定暴露 `VideoStreamInterface`、`CompleteVideoFrame` 和 `UdpSender`。
- 支持 RGB24、MJPEG、NV12 三种像素格式。
- C++ 接收侧使用 epoll 和帧分片重组统计，便于观察丢包、延迟和吞吐。

## 目录结构

```text
communication/
├── CMakeLists.txt
├── README.md
├── include/
│   └── udp_common.h
├── python/
│   └── example_python_inference.py
└── src/
    ├── 310B_udp.cpp
    ├── pc_udp.cpp
    ├── pynq_udp.cpp
    └── video_stream_interface_pybind.cpp
```

## 构建

在本目录执行：

```bash
cmake -S . -B build
cmake --build build -j
```

生成目标：

```text
build/pc_udp
build/310B_udp
build/pynq_udp
build/video_stream_interface*.so
```

如果系统没有预装 pybind11，`CMakeLists.txt` 会尝试通过 FetchContent 获取 pybind11。网络受限时，建议提前在环境中安装 pybind11 的 CMake 包。

Python 使用时设置模块路径：

```bash
export PYTHONPATH="/home/bb/competition_ws/ciciec_ascend/code/communication/build:${PYTHONPATH}"
```

## 默认网络参数

来源：`include/udp_common.h`

| 常量 | 默认值 | 说明 |
|---|---|---|
| `PC_IP` | `192.168.137.1` | PC 后端/接收端 |
| `PYNQ_IP` | `192.168.2.99` | PYNQ 视频采集端 |
| `ASCEND_310B_IP` | `192.168.2.2` | 310B 与 PYNQ 通信网口 |
| `ASCEND_310B_PC_IP` | `192.168.137.100` | 310B 与 PC 通信网口 |
| `PC_PORT` | `5555` | PC 端视频转发接收端口 |
| `PYNQ_PORT` | `9090` | PYNQ/310B 视频流接收端口 |
| `PC_CTRL_PORT` | `5566` | PC 控制/推理结果端口 |
| `PC_VIDEO_CTRL_PORT` | `5567` | 视频播放控制端口 |
| `VIDEO_WIDTH` | `640` | 默认宽度 |
| `VIDEO_HEIGHT` | `480` | 默认高度 |
| `VIDEO_FPS` | `15` | 默认帧率 |

现场 IP 不同时，应先同步修改 `include/udp_common.h`，并检查智能互动系统中的 `run_pipeline.py` 后端地址。

## 视频分片协议

每个视频 UDP 包由 32 字节 `VideoPacketHeader` 加载荷组成。

核心字段：

- `magic`：固定协议魔数 `VID0`。
- `version`：协议版本，当前为 `1`。
- `msg_type`：当前视频分片为 `VIDEO_FRAGMENT`。
- `frame_id`：帧序号。
- `frag_idx` / `frag_cnt`：分片编号和总分片数。
- `payload_bytes`：当前分片有效载荷字节数。
- `width` / `height`：帧分辨率。
- `pixel_format`：`RGB24`、`MJPEG` 或 `NV12`。
- `timestamp_us`：采集时间戳。

重要限制：

- `UDP_SAFE_PAYLOAD = 1200`，避免链路层分片。
- `MAX_VIDEO_FRAME_BYTES = 16 MB`，防止异常包占满内存。
- `FRAME_TIMEOUT_MS = 800`，C++ 侧帧重组超时丢弃。

## C++ 程序

### `pynq_udp`

PYNQ 端采集程序，使用 V4L2 mmap 从摄像头读取帧，按协议分片后发送到 310B。

```bash
./build/pynq_udp
```

典型日志：

```text
[PYNQ] camera ready
[PYNQ] send frame=... bytes=... fmt=...
```

### `310B_udp`

310B 中继/接口程序，监听 `ASCEND_310B_IP:PYNQ_PORT`，可接收 PYNQ 视频分片并进行转发或回调处理。

```bash
./build/310B_udp
```

典型日志：

```text
[310B] start relay loop
[310B] forward video frame=...
```

### `pc_udp`

PC 端重组视频并调用 ffmpeg 写出 MP4，用于验证 PYNQ -> 310B -> PC 链路。

```bash
./build/pc_udp
```

典型日志：

```text
[PC] start ffmpeg
[PC] write frame ok=...
```

## Python 绑定

构建完成后可在 Python 中导入：

```python
import video_stream_interface as vsi

def on_frame(frame):
    print(frame.frame_id, frame.width, frame.height, frame.pixel_format)

interface = vsi.VideoStreamInterface(vsi.PYNQ_PORT)
interface.set_frame_callback(on_frame)
interface.start_background()
```

关键接口：

- `VideoStreamInterface(listen_port=PYNQ_PORT)`：绑定 `ASCEND_310B_IP:listen_port` 并接收视频分片。
- `set_frame_callback(fn)`：注册完整帧回调。
- `start()` / `start_background()` / `stop()`：控制接收循环。
- `get_detailed_stats()`：返回包数、丢包、帧数、组帧延迟等统计。
- `get_udp_sender()`：获取 `UdpSender`。
- `UdpSender.send_inference_result(json_result, dst_ip=PC_IP, dst_port=PC_CTRL_PORT)`：发送 JSON 推理结果。
- `UdpSender.send_control_to_pynq(msg, dst_ip=PYNQ_IP, dst_port=PYNQ_PORT)`：发送控制消息到 PYNQ。

完整示例见 `python/example_python_inference.py`。

## 与智能互动系统的关系

`code/ascend_310B/smart_ip_interaction/src/communication/udp_video_source.py` 会把 `VideoStreamInterface` 适配成类似 `cv2.VideoCapture` 的接口：

```python
cap = UDPVideoSource(port=9090, module_path="/path/to/communication/build")
ok, frame = cap.read()
```

`run_pipeline.py` 默认配置为：

```python
USE_UDP_STREAM = True
UDP_LISTEN_PORT = 9090
```

因此真实硬件链路中需要先保证 PYNQ 正在向 310B 的 `9090` 端口发送视频分片，并保证 Python 能导入本目录编译出的 `video_stream_interface*.so`。

## 运行顺序

### 纯 C++ 链路验证

1. PC 启动 `./build/pc_udp`。
2. 310B 启动 `./build/310B_udp`。
3. PYNQ 启动 `./build/pynq_udp`。
4. 观察 PC 是否持续写入帧并生成 `output.mp4`。

### Python 推理链路

1. 编译并设置 `PYTHONPATH`。
2. 启动智能互动后端和播放器。
3. 在 310B 上运行 `smart_ip_interaction/run_pipeline.py`。
4. 启动 PYNQ 端视频发送。
5. 观察 `run_pipeline.py` 中的 `[STATS]` 接收统计和 `[ACTION]` / `[EXPR]` 事件日志。

## 排错清单

- 确认 IP 地址与实际网卡一致，尤其是 `ASCEND_310B_IP` 和 `PC_IP`。
- 确认 310B 上 `9090` 端口未被其他进程占用。
- 确认 `video_stream_interface*.so` 在 `PYTHONPATH` 或 `UDP_MODULE_PATH` 指向的目录中。
- 如果 CMake 拉取 pybind11 失败，改用本地安装的 pybind11。
- 如果 PC 端无法生成 MP4，检查 ffmpeg 是否可用。
- 如果丢包率高，优先检查网线/交换机、MTU、CPU 占用和 socket 缓冲区限制。
