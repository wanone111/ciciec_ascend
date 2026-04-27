# Ascend 310B Pose 服务

## 模块定位

本模块负责 310B 侧的单 stream 姿态推理与联动控制：

- 接收 PYNQ 视频 UDP 包（兼容 ROI1 协议与 communication 的 VID0 协议）。
- 执行姿态推理与动作判定。
- 将推理结果发送到 PC（UDP JSON）。
- 将原始视频包旁路转发到 PC（UDP）。
- 基于动作结果向 PC 发送视频播放控制指令。

当前目标：先保证 310B 推理结果闭环可稳定运行，在此基础上降低旁路丢包。

## 当前状态

- 已完成：communication 协议接入（支持 VID0 + NV12 重组进推理）。
- 已完成：单 stream 推理主链路（收包 -> 入队 -> 推理 -> 结果回传）。
- 已完成：视频旁路转发（独立线程 + 大队列，缓解高包率拥塞）。
- 已完成：视频联动控制（wave/heart 预生成视频触发）。
- 已完成：工程可编译，目标文件为 `build/ascend310b_pose`。
- 待完成：现场压测与丢包率基线固化。
- 待完成：表情链路（人脸检测/表情分类）。

## 目录结构

```text
ascend_310b/
├── CMakeLists.txt
├── README.md
├── config/
│   └── aipp_static.cfg
├── include/
│   ├── ip_video_player.h
│   └── pose_protocol.h
├── models/
│   └── yolo26/
│       ├── yolo26s-pose.om
│       └── yolo26s-pose.pt
├── scripts/
│   └── build.sh
├── src/
│   ├── ip_video_player.cpp
│   └── main.cpp
└── toolchain/
      └── aarch64.cmake
```

## 关键端口约定

- PYNQ -> 310B 输入：`--bind-port`（默认 `9090`）
- 310B -> PC 推理结果：`--peer-port`（默认 `5566`）
- 310B -> PC 视频旁路：`--video-forward-port`（默认 `5555`）
- 310B -> PC 视频控制：`--video-port`（默认 `5567`）

## 构建

在 `code/ascend_310b` 目录执行：

```bash
cmake -S . -B build
cmake --build build -j4
```

生成可执行文件：`build/ascend310b_pose`

说明：

- CMake 已处理 Ascend include 自动探测。
- CMake 会根据主机架构选择匹配的 stub 库路径（x86_64 或 aarch64）。

## 运行

推荐显式传入模型路径（当前仓内模型位于 `models/yolo26`）：

```bash
./build/ascend310b_pose \
   --model models/yolo26/yolo26s-pose.om \
   --bind-ip 192.168.2.2 --bind-port 9090 \
   --peer-ip 192.168.137.1 --peer-port 5566 \
   --video-forward-ip 192.168.137.1 --video-forward-port 5555 \
   --video-pc-ip 192.168.137.1 --video-port 5567 \
   --videos-dir /home/data/videos
```

## 命令行参数

| 参数 | 说明 | 默认值 |
| --- | --- | --- |
| `--model` | 姿态模型路径 | `yolo26/yolo26s-pose.om` |
| `--bind-ip` | 接收视频流绑定 IP | `192.168.2.2` |
| `--bind-port` | 接收视频流端口 | `9090` |
| `--peer-ip` | 推理结果目标 IP | `192.168.137.1` |
| `--peer-port` | 推理结果目标端口 | `5566` |
| `--video-forward-ip` | 视频旁路目标 IP | `192.168.137.1` |
| `--video-forward-port` | 视频旁路目标端口 | `5555` |
| `--video-pc-ip` | 视频控制目标 IP | `192.168.137.1` |
| `--video-port` | 视频控制目标端口 | `5567` |
| `--videos-dir` | 预生成视频目录 | `/home/data/videos` |
| `--device` | Ascend 设备 ID | `0` |

## 协议说明

### 输入视频协议

- 主协议：`communication/include/udp_common.h` 中定义的 `VideoPacketHeader`（VID0）。
- 兼容协议：`include/pose_protocol.h` 中 ROI1 头。
- 对于 VID0，推理链路当前按 NV12 重组并送入 NPU。

### 推理结果（310B -> PC）

UDP 文本 JSON，示例：

```json
{
   "frame_id": 123,
   "timestamp_ms": 1714200000123,
   "kps_conf_mean": 0.8123,
   "action_flag": 1,
   "action_conf": 0.9345
}
```

动作标识：

- `0`：未知/无动作
- `1`：挥手
- `2`：欢呼

### 视频控制（310B -> PC）

UDP 文本 JSON，示例：

```json
{"cmd":"play","video":"/home/data/videos/wave1.mp4","frame_id":123}
```

也支持：

- `{"cmd":"stop"}`
- `{"cmd":"pause"}`
- `{"cmd":"resume"}`

## 运行时日志关注项

### 推理链路

关注日志：

- `frame_id=... action_flag=... action_conf=... infer_us=...`

用于确认推理闭环是否稳定。

### 旁路链路

每 5000 包打印统计：

- `bypass_total`
- `ok`
- `send_drop`
- `queue_drop`
- `loss_pct`

判读建议：

- `queue_drop` 高：接收速率高于旁路发送速率，优先调队列和发送线程策略。
- `send_drop` 高：链路拥塞或对端接收瓶颈，优先看网络与 PC 接收程序。

## 系统链路

```text
PYNQ 视频流 -> 310B 收包/重组 -> NPU 推理 -> 结果(5566) -> PC
                               |
                               +-> 视频旁路(5555) -------> PC
                               |
                               +-> 视频控制(5567) -------> PC 播放器
```

## 已知事项

- 代码中的 `--model` 默认值仍为 `yolo26/yolo26s-pose.om`，而当前仓内模型在 `models/yolo26/`。
- 现场运行建议始终显式传 `--model models/yolo26/yolo26s-pose.om`。
- 目前优先级是推理闭环与旁路丢包收敛，表情链路暂未接入。
