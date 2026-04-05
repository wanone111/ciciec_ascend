# 双板通信代码

当前目录用于打通 PYNQ-Z2 到 PC/310B 的视频链路，通信层采用 ZeroMQ。

## 1. 文件说明

- `video_stream.h`：视频帧协议头定义。
- `video_pub.cpp`：发送端（PYNQ 侧），含摄像头采集 + ROI + 轻量 ISP + YUV420 打包发送。
- `video_sub.cpp`：接收端（PC 或 310B 侧），含验帧、延迟/FPS 统计、可视化预览。
- `pc_latency_client.py`：小包 RTT 延迟测试。
- `pc_throughput_client.py`：TCP 吞吐测试。

## 2. 协议约束（当前版本）

- `magic` 必须是 `VS_MAGIC`。
- `version` 必须是 `VS_VERSION`。
- `pixel_format` 目前仅支持 `VS_FMT_YUV420P8`。
- `payload_bytes` 必须等于 `width * height * 3 / 2`。

备注：当前 `crc32` 字段预留，尚未启用校验。

## 3. 依赖安装

Ubuntu / Debian 示例：

```bash
sudo apt-get update
sudo apt-get install -y build-essential pkg-config libzmq3-dev libopencv-dev
```

## 4. 编译

在当前目录执行：

```bash
g++ -O2 -std=c++17 video_pub.cpp -o video_pub $(pkg-config --cflags --libs opencv4 libzmq)
g++ -O2 -std=c++17 video_sub.cpp -o video_sub $(pkg-config --cflags --libs opencv4 libzmq)
```

## 5. 启动顺序（推荐）

### 5.1 在接收端（PC 或 310B）启动

```bash
./video_sub --connect tcp://<PYNQ_IP>:5555 --show 1 --hwm 2
```

退出按键：`q` 或 `ESC`。

### 5.2 在发送端（PYNQ）启动

```bash
./video_pub --bind tcp://*:5555 --camera 0 --width 640 --height 480 --fps 30 --roi 0,0,1,1 --gamma 2.2 --denoise 3 --show 0
```

常用 ROI 示例（中心区域）：

```bash
--roi 0.125,0.125,0.75,0.75
```

## 6. 打通判据

满足以下条件可认为“视频通路已打通”：

1. `video_pub` 无持续报错（摄像头可读、发送成功）。
2. `video_sub` 能持续显示画面。
3. `video_sub` 每秒输出 FPS/平均延迟统计，且帧号持续递增。
4. `invalid_or_drop` 不持续快速增长。

## 7. 常见问题定位

1. 收不到帧：先检查 IP/端口是否一致，再检查发送端是否 `--bind tcp://*:5555`。
2. 摄像头打开失败：确认设备索引，常见是 `/dev/video0` 对应 `--camera 0`。
3. FPS 很低：先把 `--denoise` 调小、`--roi` 缩小，再观察统计变化。
4. 延迟升高：优先检查网络拥塞和接收端显示负载，必要时使用 `--show 0` 仅做链路验证。

## 8. 下一步（面向 310B）

1. 在 310B 侧复用 `video_sub.cpp` 的协议解析逻辑。
2. 将 YUV420 转模型输入格式的步骤放在 310B，避免 PYNQ 侧重复计算。
3. 控制通道新增 REQ/REP，用于在线调整 ROI、gamma 和降噪参数。
