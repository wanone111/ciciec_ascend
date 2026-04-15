# UDP 视频流通信说明（PYNQ <-> PC）

本目录当前阶段用于验证 PYNQ 到 PC 的 UDP 视频流传输，并保留低频 ACK 控制回路。

## 当前状态

- 已完成 PYNQ 与 PC 的 UDP 基础互通验证。
- 当前主线链路为：PS 直采 USB 摄像头 -> RGB24 分片 UDP -> PC 重组并落盘。
- 接收端执行整帧完整性校验：缺包即丢整帧，不做重传。
- 当前目标参数：1280x720@30，RGB 原始帧。

## 当前联通关系

- PYNQ 由 PS 端直接采集 USB 摄像头帧数据并发送分片流到 PC。
- PC 持续接收并重组分片，只有完整帧才进入显示/统计流程。
- PC 每 30 帧回发一次 ACK，PYNQ 接收 ACK 仅用于链路健康观测。

## 文件说明

- [include/udp_common.h](include/udp_common.h) 提供 UDP 公共函数、IP 宏和端口常量。
- [src/client_udp.cpp](src/client_udp.cpp) 是 PC 端程序，负责分片重组、整帧校验、可选显示与周期 ACK。
- [src/pynq_udp.cpp](src/pynq_udp.cpp) 是 PYNQ 端程序，负责从 PS 侧 OpenCV 采集帧、转换为 RGB24 后分片发送与周期 ACK 接收。
- [CMakeLists.txt](CMakeLists.txt) 提供 CMake 构建入口。

## 参数配置

请在 [include/udp_common.h](include/udp_common.h) 中确认以下宏值已经填写正确：

- `PC_IP`
- `PYNQ_IP`
- `PC_PORT`
- `PYNQ_PORT`

视频参数（定义在 `udp_common.h`）：

- `VIDEO_WIDTH = 1280`
- `VIDEO_HEIGHT = 720`
- `VIDEO_FPS = 30`
- `UDP_PAYLOAD_BYTES = 1200`
- `ACK_EVERY_N_FRAMES = 30`

推荐新增参数（用于 RGB 方案文档化）：

- `PIXEL_FORMAT = RGB24`
- `FRAME_BYTES = VIDEO_WIDTH * VIDEO_HEIGHT * 3`

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

- PYNQ 端：默认启用 OpenCV 采集（`ENABLE_OPENCV_CAPTURE=ON`）。
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

若需要显式关闭 PYNQ 端 OpenCV 采集编译开关（不推荐当前方案），使用：

```bash
cmake -S . -B build -DENABLE_OPENCV_CAPTURE=OFF
cmake --build build -j
```

## PYNQ 本地 g++ 编译（不经 CMake）

在通信目录下执行：

```bash
mkdir -p build
g++ -std=c++17 -O2 -pthread -DHAVE_OPENCV=1 -Iinclude src/pynq_udp.cpp -o build/pynq_udp $(pkg-config --cflags --libs opencv4)
```

若系统使用 `opencv` 而非 `opencv4`，将命令中的 `opencv4` 替换为 `opencv`。

生成目标：

- `build/pc_udp`
- `build/pynq_udp`

## 运行顺序

1. 先在 PC 上运行 `./build/pc_udp`。
2. 再在 PYNQ 上运行 `./build/pynq_udp`。
3. 两端持续运行，观察帧重组和 ACK 日志是否稳定。

## 最短可运行命令清单（复制即跑）

先决条件：

1. 已在 [include/udp_common.h](include/udp_common.h) 中配置 `PC_IP/PC_PORT/PYNQ_PORT`。
2. PC 与 PYNQ 在同一网段。

PC 端（接收 + 落盘）：

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/communication
mkdir -p build
cmake -S . -B build
cmake --build build -j
export UDP_SAVE_PATH=$PWD/build/recv_output.rgb
./build/pc_udp
```

PYNQ 端（本地编译 + 发送）：

```bash
cd ~/jupyter_notebooks/<your_project>/ciciec_ascend/code/communication
mkdir -p build
g++ -std=c++17 -O2 -pthread -DHAVE_OPENCV=1 -Iinclude src/pynq_udp.cpp -o build/pynq_udp $(pkg-config --cflags --libs opencv4)
export UDP_CAMERA_DEV=/dev/video0
./build/pynq_udp
```

若 PYNQ 上没有 `opencv4.pc`，将命令中的 `opencv4` 改为 `opencv`。

最小验证：

```bash
ls -lh /home/bb/competition_ws/ciciec_ascend/code/communication/build/recv_output.rgb*
```

运行后应看到：

1. `recv_output.rgb` 文件持续增长。
2. `recv_output.rgb.index.csv` 中 `frame_id` 持续递增。
3. 终端日志中的 `complete` 持续递增。

可选显示：

- 若希望 PC 显示视频窗口，可在 PC 端启动前设置环境变量：`export UDP_SHOW=1`。
- 若构建时未找到 OpenCV，`UDP_SHOW=1` 会自动降级为仅日志模式。
- 若不设置，PC 端默认只打印日志与统计信息。

PYNQ PS 直采输入：

- 默认采集设备为 `/dev/video0`。
- 建议先探测摄像头可用模式，再锁定 1280x720@30。
- 当前实现基于 OpenCV 采集，发送前统一转换为 RGB24 连续内存，避免接收端格式歧义。
- `UDP_CAMERA_DEV` 可设置为 `/dev/videoX`；若设置为纯数字（如 `0`），会自动映射为 `/dev/video0`。
- 未设置 `UDP_CAMERA_DEV` 时，会依次尝试 `/dev/video0`、`/dev/video1`。

PC 侧落盘输出：

- 默认输出原始文件：`recv_output.rgb`。
- 默认输出索引文件：`recv_output.rgb.index.csv`。
- 可通过环境变量指定输出路径：`export UDP_SAVE_PATH=/path/to/output.rgb`。

## 成功判定

当双向通信成功时，日志应持续出现：

- PYNQ 端持续打印已发送帧统计日志。
- PC 端持续打印完整帧统计日志（`complete` 递增）。
- PC 端按 30 帧间隔打印 `send ACK ok`。
- PYNQ 端按 30 帧间隔打印 `recv ACK` 或超时日志。

补充：

- 手动按 `Ctrl+C` 结束程序时，终端返回 `Exit Code: 130` 属于正常退出现象。

## 协议行为

- 像素格式：当前基线为 NV12，最小可行目标为 RGB24。
- 传输策略：UDP 应用层分片。
- 丢包策略：只显示完整帧，缺包丢整帧。
- 重传策略：不重传（低延迟优先）。
- ACK 策略：每 30 帧 ACK 一次。

说明：

- 当前头结构沿用 `frame_id/seq_num/total/payload_len/timestamp`。
- 若后续发现跨端兼容或扩展不足，再在文档中补充 `magic/version` 等字段。

## 常见问题

- 若 PYNQ 端摄像头打开失败，请先检查 `/dev/video0` 是否存在并具备访问权限。
- 若出现 `can't open camera by index` 或 `open camera failed; candidates=0`，请显式使用设备路径：`export UDP_CAMERA_DEV=/dev/video0`。
- 若当前用户不在 `video` 组，可能无法读取摄像头设备，需将用户加入 `video` 组后重新登录。
- 若帧率不稳定，请确认摄像头确实支持 1280x720@30，并关闭自动曝光/自动对焦抖动源。
- 若 PC 端 `drop` 持续上升，通常是网络丢包或接收处理跟不上导致。
- 若两端同网段仍收不到包，请检查防火墙是否放行 UDP `5555/9090`。
- 若 CMake 报 OpenCV 未找到，请先安装 OpenCV 开发包后重新配置构建。

## 最小可行方案（按当前需求）

目标约束：

- 分辨率与帧率：1280x720@30
- 数据格式：RGB24 原始帧
- 采集路径：PYNQ PS 直采 USB 摄像头
- 传输协议：沿用当前自定义 UDP 分片头
- 可靠性：允许丢帧，不做重传
- PC 侧：C++ 接收、重组、直接保存原始帧，并提供实时预览
- 时间戳：保留并用于端到端时延统计（需双端时钟同步）

落地步骤：

1. PYNQ 端固定采集参数为 1280x720@30，并在发送前转为 RGB24。
2. 按 `UdpHeader + payload` 分片发送，`frame_id` 每帧递增。
3. PC 端按 `frame_id/seq_num/total` 重组，缺包帧直接丢弃。
4. 对完整帧直接追加写入原始文件（`.rgb`），并记录帧索引与时间戳。
5. 开启低延迟预览时，预览与写盘线程解耦，接收线程只做重组与入队。

## 优化方向

1. 协议增强：在头中增加 `magic/version/pixel_format/frame_bytes`，降低跨版本歧义。
2. 内存优化：发送端避免每包动态分配，改预分配连续缓冲或环形队列。
3. 接收优化：重组、预览、写盘分线程，减少互相阻塞。
4. 网络优化：调大 `SO_SNDBUF/SO_RCVBUF`，按链路情况评估 jumbo frame。
5. 计时优化：双端统一 NTP/Chrony（或 PTP）后再使用单向时延指标。
6. 演进方向：后续可引入轻量压缩或 RTP 封装，提升带宽裕量与互操作性。

## 快速回归清单

1. 确认 [include/udp_common.h](include/udp_common.h) 中 IP 与端口与现场一致。
2. 在通信目录执行一次 CMake 全量编译。
3. 先启动 `pc_udp`，再启动 `pynq_udp`。
4. 观察 `complete`、`dropped` 与 ACK 日志是否符合预期。
5. 连续运行 10 分钟，确认无持续性异常。
6. 检查 `recv_output.rgb` 大小是否与完整帧数匹配（每帧 `1280*720*3` 字节）。
