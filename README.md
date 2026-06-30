# 集创赛 华强 x 昇腾赛题 - 智能视觉交互系统

本仓库是一个面向文旅展演、互动大屏和主题乐园场景的智能视觉交互终端工程。系统采用 **PYNQ-Z2 FPGA + Ascend 310B NPU + PC 后端/播放器** 的异构架构，实现从视频采集、前端预处理、姿态/表情识别，到熊二 IP 视频联动的端到端演示链路。

当前主线能力：

- 姿态识别：基于 YOLO-Pose 关键点识别挥手、比心、欢呼。
- 表情识别：YuNet 人脸检测 + MobileNetV3 6 分类表情识别。
- 视频联动：根据动作/表情事件切换熊二互动视频，播完回到待机视频。
- 后端分发：Ascend 310B 推理进程通过 TCP 推送事件，PC 后端通过 WebSocket 广播控制指令。
- 三端通信：PYNQ -> 310B -> PC 的 UDP 视频分片链路和 C++/pybind11 视频流接口。
- 角色语音对话：ASR + LLM + TTS 模块已拆分实现，仍处于联调和集成阶段。

## 当前进度快照（2026-05-04）

本轮已完成 PYNQ、Ascend 310B、PC 三端真实硬件联调，并将姿态、人脸检测、表情分类三条视觉推理路径统一到 **ACL + OM + NPU**。日志目录如下：

| 端侧 | 日志目录 | 主要文件 |
|---|---|---|
| PYNQ-Z2 | `/home/xilinx/jupyter_notebooks/ciciec/log` | `pynq.jsonl` |
| Ascend 310B | `/root/userdata/ciciec/ascend_310B/log/` | `pipeline.jsonl` |
| PC | `/home/bb/competition_ws/ciciec_ascend/log` | `backend.jsonl`、`player.jsonl` |

最新已归档 run 位于：

```text
/home/bb/competition_ws/ciciec_ascend/log/e2e_20260504_155646
```

该目录包含多次启动追加日志，以下指标按最后一次成功 `pipeline_start` 重新截取统计：

| 指标 | 当前结果 |
|---|---|
| PYNQ 发送帧数 | 690 帧 |
| PYNQ 实际发送帧率 | 约 15.02 FPS |
| PYNQ 发送失败包 | 0 |
| 310B 处理帧数 | 498 帧 |
| 310B 有效处理帧率 | 约 11.17 FPS |
| 310B 汇报 FPS | 平均 11.60，P95 14.87 |
| UDP 包/帧丢失率 | 0.00% / 0.00% |
| UDP 组帧平均延迟 | 80.77 ms |
| 源帧缺失率 | 13 / 511，约 2.54% |
| 姿态推理后端 | `acl_om_npu` |
| 人脸检测后端 | `acl_om_npu` |
| 表情分类后端 | `acl_om_npu` |
| 310B 姿态推理耗时 | 平均 45.65 ms，P50 44.84 ms，P95 51.32 ms |
| 表情分支耗时 | 平均 24.09 ms，P50 23.10 ms，P95 31.95 ms |
| 310B 单帧总耗时 | 平均 69.99 ms，P50 65.55 ms，P95 94.80 ms |
| PC 后端到播放器切换 | 平均约 1.01 ms |
| 播放器实际切换 | 16 次，`heart_response` 14 次，`wave_response` 2 次 |

当前判断：

- 三端互通已经跑通，PYNQ 采集发送、UDP 分片重组、310B NPU 推理事件、PC 后端和播放器联动均有日志闭环。
- 视觉推理侧已确认不再走 CPU/PT/PTH 常规回退路径；`pipeline.jsonl` 中应能看到 `pose_runtime`、`face_runtime`、`expression_runtime` 均为 `acl_om_npu`。
- 通信链路表现稳定，本轮 PYNQ 发送失败包为 0，UDP 包/帧丢失率为 0。
- 姿态推理从秒级 CPU/PT 路径收敛到约 45.65 ms/帧，当前主要耗时变为姿态 OM 推理 + 表情分支 + UDP 组帧综合开销。
- 本轮事件以 `HEART` 为主，少量 `WAVE` 和 1 次 `CHEER`；表情输出以 `angry`、`neutral` 为主，仍需带标注样本复核阈值和类别映射。
- 播放器仍有 26 次 missing，其中 `ar_overlay` 14 次、`angry_response` 8 次、`wow_response` 3 次、`cheer_effect` 1 次；需要补齐素材或调整后端映射。
- 本轮 trial CSV 仍是模板时间戳，准确率 0% 属于标注无效结果，不能作为最终动作/表情准确率。

下一步优先事项：

1. 每次压测使用新的 run 目录或清空旧 JSONL，避免多次启动日志污染汇总。
2. 采集真实 `action_trials.csv` 和 `expression_trials.csv`，按 `WAVE/HEART/CHEER` 各 10 次、6 类表情各 10 次生成准确率表。
3. 补齐 `ar_overlay`、`angry_response`、`wow_response`、`cheer_effect` 等视频资源，或暂时关闭缺失素材映射。
4. 针对 `HEART` 和 `angry/neutral` 偏多的问题，复核动作规则阈值、现场光照和表情模型类别阈值。
5. 复测 30 到 60 分钟稳定性，并同步观察 `npu-smi info`、`pipeline.jsonl` runtime 字段和帧率波动。

---

## 系统架构

```text
┌─────────────┐   UDP 视频流 :9090   ┌─────────────┐   TCP :9000   ┌─────────────┐
│  PYNQ-Z2    │ ───────────────────> │ Ascend 310B │ ────────────> │  PC 后端     │
│ 视频采集/PL │                       │ run_pipeline│                │ server.py    │
│ ISP/ROI/HLS │                       │ 姿态+表情推理│                │ WS :8000     │
└─────────────┘                       └─────────────┘                └──────┬──────┘
                                                                              │
                                                                              │ WebSocket
                                                                              ▼
                                                                       ┌─────────────┐
                                                                       │ video_player│
                                                                       │ IP 视频联动  │
                                                                       └─────────────┘
```

### 主要数据流

1. PYNQ-Z2 侧负责视频采集、HLS 预处理 IP、UDP 分片发送。
2. Ascend 310B 侧运行 `run_pipeline.py`，接收 UDP 视频流或本地摄像头，执行姿态识别、动作规则判断、人脸检测和表情分类。
3. 推理事件经 TCP 发送到 PC 后端 `backend/server.py`。
4. 后端维护剧情状态机，通过 WebSocket `/ws` 广播控制消息。
5. `scripts/video_player.py` 根据 WebSocket 或 UDP 控制消息切换互动视频。

---

## 目录结构

```text
.
├── README.md
├── docs/
│   ├── 赛题.md
│   └── 智能视觉交互系统报告.md
├── Scripts/
│   ├── README.md
│   ├── ascend_310b/
│   │   └── set_env.sh
│   ├── installation/
│   └── pynq_z2/
├── code/
│   ├── ascend_310B/
│   │   ├── README.md
│   │   ├── smart_ip_interaction/
│   │   │   ├── run_pipeline.py
│   │   │   ├── backend/server.py
│   │   │   ├── configs/video_config.json
│   │   │   ├── scripts/
│   │   │   │   ├── launch_pipeline.py
│   │   │   │   ├── launch_local_test.py
│   │   │   │   ├── simulate_310b.py
│   │   │   │   ├── fake_frontend_client.py
│   │   │   │   └── video_player.py
│   │   │   ├── src/
│   │   │   │   ├── behavior/
│   │   │   │   ├── communication/
│   │   │   │   ├── inference/
│   │   │   │   └── interaction/
│   │   │   ├── models/
│   │   │   ├── datasets/
│   │   │   └── training/
│   │   └── character_voice_chat/
│   │       ├── asr/
│   │       ├── audio/
│   │       ├── core/
│   │       ├── llm/
│   │       ├── output/
│   │       ├── rag/
│   │       ├── README.md
│   │       └── main.py
│   ├── communication/
│   │   ├── include/udp_common.h
│   │   ├── src/
│   │   ├── python/example_python_inference.py
│   │   ├── CMakeLists.txt
│   │   └── README.md
│   └── pynq_z2/
│       ├── hls/video_preprocess/
│       ├── pl/
│       ├── vivado/
│       └── README.md
```

---

## 快速开始

以下命令默认从仓库根目录 `/home/bb/competition_ws/ciciec_ascend` 执行。

### 1. 准备 Python 环境

按实际板端/本地环境激活 Conda 或 Python 虚拟环境，然后安装常用依赖：

```bash
pip install opencv-python numpy pillow
pip install fastapi uvicorn websockets requests sounddevice soundfile websocket-client
```

310B 生产推理默认使用 `models/om/*.om`，姿态、人脸检测和表情分类都走 `ACL + OM + NPU`。只有本地调试 PT/PTH 回退或训练脚本才需要额外安装：

```bash
pip install ultralytics torch torchvision
```

如需 Ascend/CANN 相关环境，可按现场路径调整后加载：

```bash
source Scripts/ascend_310b/set_env.sh
```

### 2. 编译通信模块

`run_pipeline.py` 默认使用 UDP 视频流时，需要 `video_stream_interface` pybind11 模块。

```bash
cd code/communication
cmake -S . -B build
cmake --build build -j
export PYTHONPATH="$(pwd)/build:${PYTHONPATH}"
```

说明：

- `CMakeLists.txt` 会构建 `pc_udp`、`310B_udp`、`pynq_udp` 和 `video_stream_interface`。
- 如果系统未预装 pybind11，CMake 会尝试通过 FetchContent 获取 pybind11。
- `run_pipeline.py` 当前也支持通过 `UDP_MODULE_PATH` 指定 `communication/build` 的绝对路径。

### 3. 一键启动演示链路

```bash
cd code/ascend_310B/smart_ip_interaction
python scripts/launch_pipeline.py
```

该脚本会按顺序启动：

- `backend/server.py`：TCP 9000 事件接收 + WebSocket 8000 广播。
- `scripts/video_player.py`：熊二互动视频播放器。
- `run_pipeline.py`：姿态识别、表情识别、事件发送。

### 4. 分步启动

如果需要分开观察日志，可用三个终端运行：

```bash
cd code/ascend_310B/smart_ip_interaction
python backend/server.py
```

```bash
cd code/ascend_310B/smart_ip_interaction
python scripts/video_player.py
```

```bash
cd code/ascend_310B/smart_ip_interaction
python run_pipeline.py
```

后端状态检查：

```bash
curl http://127.0.0.1:8000/status
```

### 5. 本地模拟测试

不接硬件时，可用模拟事件验证后端和播放器：

```bash
cd code/ascend_310B/smart_ip_interaction
python scripts/launch_local_test.py
```

也可以手动发送事件：

```bash
python scripts/simulate_310b.py --mode interactive
```

---

## 关键配置

### `run_pipeline.py` 视频源

位置：`code/ascend_310B/smart_ip_interaction/run_pipeline.py`

```python
USE_UDP_STREAM = True
UDP_LISTEN_PORT = 9090
UDP_MODULE_PATH = "/root/userdata/ciciec/communication/build"

BACKEND_HOST = "192.168.137.1"
BACKEND_PORT = 9000
```

- `USE_UDP_STREAM = True`：使用 PYNQ -> 310B 的硬件 UDP 视频流。
- `USE_UDP_STREAM = False`：使用本地摄像头或视频文件，`VIDEO_SOURCE = 0` 表示默认摄像头。
- `UDP_MODULE_PATH` 应指向编译生成 `video_stream_interface` 的目录；也可设置 `PYTHONPATH` 后改为 `None`。

### `video_player.py` 控制通道

位置：`code/ascend_310B/smart_ip_interaction/scripts/video_player.py`

```python
WS_URL = "ws://127.0.0.1:8000/ws"
PC_VIDEO_CTRL_PORT = 5567
```

播放器优先监听后端 WebSocket 指令，同时保留 UDP 控制端口 `5567` 作为兼容通道。
默认情况下，互动视频播放期间会忽略新的切换指令，避免动作/表情误检打断当前视频；如确实需要允许打断，可设置 `SMART_IP_PLAYER_ALLOW_INTERRUPT=1`。

### Ascend NPU 推理提速开关

位置：`code/ascend_310B/smart_ip_interaction/run_pipeline.py`

以下环境变量用于硬件压测和现场调参：

```bash
# 表情分支频率：每 N 帧做人脸检测 + 表情推理，默认 3
export SMART_IP_EXPR_SKIP=3

# 表情事件最短发送间隔，默认 1.5 秒
export SMART_IP_EXPR_SEND_INTERVAL=1.5

# 压测纯推理时可关闭画框、文字、顶部状态条
export SMART_IP_ENABLE_OVERLAY=0

# 压测纯推理时可关闭 AR 道具叠加
export SMART_IP_ENABLE_AR=0

# 逐帧日志降采样，默认 1；压测长跑建议设为 5 或 10
export SMART_IP_BENCH_FRAME_STRIDE=5

# 记录 ACL H2D / execute / D2H 分段耗时
export SMART_IP_ACL_PROFILE=1
```

打开 `SMART_IP_ACL_PROFILE=1` 后，`pipeline.jsonl` 的 `frame` 记录会额外包含 `pose_acl_*`、`face_acl_*`、`expression_acl_*` 字段，用于判断瓶颈是在数据搬运还是 NPU 执行。

### 视频映射

位置：`code/ascend_310B/smart_ip_interaction/configs/video_config.json`

```json
{
  "idle": "datasets/interaction_videos/idle/idle.mp4",
  "commands": {
    "wave_response": "datasets/interaction_videos/wave/wave.mp4",
    "heart_response": "datasets/interaction_videos/heart/heart.mp4",
    "cheer_effect": "datasets/interaction_videos/cheer/cheer.mp4"
  }
}
```

当前仓库已包含部分互动视频资源，例如 `idle/idle.mp4`、`wave/wave.mp4`、`heart/heart.mp4`。如果某个配置视频不存在，播放器会打印告警并忽略对应指令。

---

## 模型与算法

### 姿态识别

入口：`src/inference/pose_infer.py`

- 310B 生产首选：`models/om/yolo26s-pose.om`
- 可选调试路径：`models/onnx/yolo26s-pose.onnx`
- 本地调试权重：`yolo26n-pose.pt`
- 输入尺寸：`640 x 640`
- 检测目标：人体 COCO 17 关键点

`PoseInfer` 默认强制 `ACL + OM + NPU`，启动时会在 `pipeline.jsonl` 写出 `pose_runtime.backend=acl_om_npu`，逐帧日志也会包含 `pose_backend=acl_om_npu`。如果 CANN/ACL 环境或 OM 文件不可用，会直接报错，避免静默回退到 CPU/PT。只有本地调试时才建议显式打开 PT 回退：

```bash
export SMART_IP_POSE_REQUIRE_NPU=0
export SMART_IP_POSE_ALLOW_PT_FALLBACK=1
```

### 动作规则

入口：`src/behavior/action_rules.py`、`src/behavior/action_state_machine.py`

| 动作 | 判定方式 |
|---|---|
| WAVE | 任一手腕高于肩膀一定比例 |
| HEART | 双腕靠近，且位于胸前区域 |
| CHEER | 双手同时高于头部/鼻子附近 |

状态机默认连续 4 帧触发，同一动作冷却 2.5 秒，并要求动作释放后才能再次触发，优先级为 `CHEER > HEART > WAVE`。现场需要调参时可设置 `SMART_IP_ACTION_TRIGGER_FRAMES`、`SMART_IP_ACTION_COOLDOWN`、`SMART_IP_ACTION_REARM_FRAMES`。

### 表情识别

入口：`src/inference/face_detector.py`、`src/inference/expression_infer.py`

- 人脸检测生产首选：`models/om/face_detection_yunet_2023mar_640.om`
- 表情分类生产首选：`models/om/emotion_mobilenetv3_6cls.om`
- 本地调试人脸 ONNX：`models/pretrained/face_detection_yunet_2023mar.onnx`
- 本地调试表情 ONNX：`models/onnx/emotion_mobilenetv3_6cls.onnx`
- 本地训练权重：`models/checkpoints/emotion_mobilenetv3_6cls_best.pth`
- 表情类别：`angry`、`fear`、`happy`、`neutral`、`sad`、`surprised`
- 平滑策略：7 帧概率均值滑动窗口

人脸检测和表情分类默认同样强制 `ACL + OM + NPU`，启动时会写出 `face_runtime.backend=acl_om_npu`、`expression_runtime.backend=acl_om_npu`，逐帧日志包含 `face_backend` 和 `expression_backend`。只有本地调试时才建议显式打开 OpenCV/PTH 回退：

```bash
export SMART_IP_FACE_REQUIRE_NPU=0
export SMART_IP_FACE_ALLOW_OPENCV_FALLBACK=1
export SMART_IP_EXPR_REQUIRE_NPU=0
export SMART_IP_EXPR_ALLOW_TORCH_FALLBACK=1
```

---

## 通信与端口

### 演示业务链路

| 端口 | 方向 | 说明 |
|---|---|---|
| 9090 | PYNQ -> Ascend 310B | UDP 视频流，`run_pipeline.py` 监听 |
| 9000 | Ascend 310B -> PC 后端 | TCP 推理事件，`backend/server.py` 监听 |
| 8000 | PC 后端 -> 播放器 | WebSocket 服务，路径 `/ws` |
| 5567 | 控制端 -> 播放器 | UDP 视频控制兼容通道 |

### 底层通信模块

来源：`code/communication/include/udp_common.h`

| 设备/参数 | 默认值 | 说明 |
|---|---|---|
| PC_IP | `192.168.137.1` | PC 后端/播放端 |
| PYNQ_IP | `192.168.2.99` | PYNQ 视频采集端 |
| ASCEND_310B_IP | `192.168.2.2` | 310B 与 PYNQ 通信网口 |
| ASCEND_310B_PC_IP | `192.168.137.100` | 310B 与 PC 通信网口 |
| PC_PORT | `5555` | PC 端视频转发接收端口 |
| PYNQ_PORT | `9090` | 视频流接收端口 |
| PC_CTRL_PORT | `5566` | PC 控制/推理结果相关端口 |
| PC_VIDEO_CTRL_PORT | `5567` | 视频播放控制端口 |

如果现场网络不同，需要同步检查 `udp_common.h`、`run_pipeline.py` 中的 `BACKEND_HOST`，以及 PYNQ/310B 实际网卡配置。

### TCP 事件格式

Ascend 310B 推理进程发送到 PC 后端的事件示例：

```json
{"type": "action", "value": "wave_hand", "keypoints": [[x1, y1], [x2, y2]]}
{"type": "action", "value": "heart_particles", "keypoints": [[x1, y1], [x2, y2]]}
{"type": "action", "value": "cheer_light_sound"}
{"type": "expression", "value": "smile_feedback", "face_bbox": [x1, y1, x2, y2]}
```

### WebSocket 控制格式

PC 后端广播给播放器/前端的控制消息示例：

```json
{"scene": "dialogue", "character": "bear", "dialogue_id": "wave_response", "tts": "你好呀！欢迎来到熊乐园！"}
{"scene": "ar_overlay", "effect": "bear_hat", "keypoints": [[x1, y1], [x2, y2]]}
{"scene": "switch_scene", "scene_id": 1, "trigger": "cheer", "cheer_total": 3}
```

---

## PYNQ-Z2 前端工程

PYNQ-Z2 相关工程位于 `code/pynq_z2/`。

主 HLS IP：

```text
code/pynq_z2/hls/video_preprocess/
```

能力包括：

- AXI4-Stream RGB/BGR 输入。
- 动态 ROI 像素级门控。
- 白平衡、Gamma、时域降噪等轻量 ISP。
- RGB 到 YUV420SP/NV12 输出。
- AXI-Lite 参数配置接口。

运行 HLS 脚本前需要先加载 Vitis 环境，具体命令以实际安装路径为准：

```bash
source /opt/Xilinx/2025.2.1/Vitis/settings64.sh
cd code/pynq_z2/hls/video_preprocess
vitis-run --mode hls --tcl scripts/run_hls.tcl
```

比特流和 HWH 文件位于 `code/pynq_z2/pl/bit/`，使用说明见 `code/pynq_z2/pl/README.md`。

---

## 角色语音对话模块

目录：`code/ascend_310B/character_voice_chat/`

该模块用于扩展熊二角色语音对话，包含：

- `asr/`：Whisper/豆包 ASR 相关适配。
- `llm/`：角色提示词和对话模型调用。
- `rag/`：简单角色知识库检索。
- `core/`：对话管理、状态机和实时流水线。
- `audio/`：麦克风输入、VAD、音频缓冲和播放。
- `output/`：控制台输出与豆包 TTS。

当前状态：模块已拆分实现，但尚未与 `smart_ip_interaction` 的动作/表情联动链路做完整集成。运行前需要根据实际包路径和 API Key 配置 Python 环境。

---

## 常用调试入口

```bash
# 后端状态
curl http://127.0.0.1:8000/status

# 本地模拟完整链路
cd code/ascend_310B/smart_ip_interaction
python scripts/launch_local_test.py

# 手动发送推理事件
python scripts/simulate_310b.py --mode interactive

# WebSocket 调试客户端
python scripts/fake_frontend_client.py

# 真实硬件端到端压测汇总
python scripts/benchmark/summarize_hardware_e2e.py \
  --pynq-log-dir /home/xilinx/jupyter_notebooks/ciciec/log \
  --ascend-log-dir /root/userdata/ciciec/ascend_310B/log \
  --pc-log-dir /home/bb/competition_ws/ciciec_ascend/log

# 通信模块 Python 示例
cd code/communication
python python/example_python_inference.py
```

---

## 当前状态与注意事项

- 姿态识别、表情识别、后端分发和视频播放器主链路已具备可演示代码。
- PYNQ-Z2 HLS 预处理工程和通信模块已在仓库中保留，硬件端到端部署需结合现场网络、Vitis/CANN 环境和板端路径调整。
- `smart_ip_interaction/scripts/benchmark/` 已补充真实硬件压测日志汇总脚本和 trial 标注模板；最终准确率、端到端延迟和稳定性数据需要由比赛设备实测日志生成。
- `run_pipeline.py` 默认使用 UDP 视频流；如果 UDP 模块或视频源不可用，本地调试时可改用摄像头/视频文件。
- 姿态、人脸检测、表情分类默认不回退到 CPU/PT/PTH；现场确认 `pipeline.jsonl` 中的 `pose_runtime`、`face_runtime`、`expression_runtime` 均为 `acl_om_npu`。
- `video_config.json` 中部分表情/欢呼视频路径可能需要按素材补齐。
- 语音对话模块仍是联调中能力，不应视为已和 IP 视频系统完整闭环。
- AIPP/DVPP 深度集成仍需根据最终模型格式和板端部署方式继续收敛。

---

## 相关文档

- 赛题说明：`docs/赛题.md`
- 系统设计与实现报告：`docs/智能视觉交互系统报告.md`
- Ascend 310B 侧代码说明：`code/ascend_310B/README.md`
- 智能 IP 互动子系统：`code/ascend_310B/smart_ip_interaction/README.md`
- Ascend NPU 推理提速 TODO：`docs/ascend_npu_inference_speedup_todo.md`
- 角色语音对话模块：`code/ascend_310B/character_voice_chat/README.md`
- 辅助脚本说明：`Scripts/README.md`
- 算法接口规范：`code/ascend_310B/smart_ip_interaction/configs/algorithm_interface_v1.0.md`
- 通信模块说明：`code/communication/README.md`
- PYNQ-Z2 预处理系统：`code/pynq_z2/README.md`
- HLS 预处理 IP：`code/pynq_z2/hls/video_preprocess/README.md`

---

## 许可证

本项目为集创赛参赛代码，仅供学习交流和比赛展示使用。
