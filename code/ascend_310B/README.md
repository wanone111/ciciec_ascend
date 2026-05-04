# Ascend 310B 侧代码说明

本目录保存 Ascend 310B/PC 侧的软件代码，主要包括两部分：

- `smart_ip_interaction/`：智能 IP 视觉互动主链路，负责姿态识别、表情识别、事件推送、后端分发和熊二互动视频播放。
- `character_voice_chat/`：熊二角色语音对话实验模块，负责 ASR、LLM、RAG、TTS 和实时语音流水线探索，目前尚未完整接入视觉互动主链路。

整体项目中，PYNQ-Z2 负责视频采集和前端预处理，Ascend 310B 侧负责视觉推理和互动事件生成，PC 后端/播放器负责展示联动。

## 目录结构

```text
code/ascend_310B/
├── README.md
├── smart_ip_interaction/
│   ├── README.md
│   ├── run_pipeline.py
│   ├── backend/
│   │   └── server.py
│   ├── configs/
│   │   ├── algorithm_interface_v1.0.md
│   │   └── video_config.json
│   ├── datasets/
│   │   └── interaction_videos/
│   ├── models/
│   ├── scripts/
│   ├── src/
│   └── training/
└── character_voice_chat/
    ├── README.md
    ├── asr/
    ├── audio/
    ├── core/
    ├── data/character_kb/
    ├── llm/
    ├── output/
    ├── rag/
    └── test_*.py
```

## 子系统定位

### `smart_ip_interaction`

这是当前可演示的主线系统：

```text
PYNQ UDP 视频流 / 本地摄像头 / 视频文件
        |
        v
run_pipeline.py
  - YOLO-Pose 姿态识别
  - WAVE / HEART / CHEER 动作规则
  - YuNet 人脸检测
  - MobileNetV3 6 类表情识别
        |
        | TCP JSON :9000
        v
backend/server.py
        |
        | WebSocket :8000/ws
        v
scripts/video_player.py
```

主要入口：

- 推理流水线：`smart_ip_interaction/run_pipeline.py`
- 后端服务：`smart_ip_interaction/backend/server.py`
- 视频播放器：`smart_ip_interaction/scripts/video_player.py`
- 一键启动：`smart_ip_interaction/scripts/launch_pipeline.py`
- 本地模拟：`smart_ip_interaction/scripts/launch_local_test.py`

详细说明见 `smart_ip_interaction/README.md`。

当前主线推理状态：

| 模块 | 生产模型 | 默认后端 |
|---|---|---|
| 姿态关键点 | `smart_ip_interaction/models/om/yolo26s-pose.om` | `ACL + OM + NPU` |
| 人脸检测 | `smart_ip_interaction/models/om/face_detection_yunet_2023mar_640.om` | `ACL + OM + NPU` |
| 表情分类 | `smart_ip_interaction/models/om/emotion_mobilenetv3_6cls.om` | `ACL + OM + NPU` |

现场运行时，`pipeline.jsonl` 的 `pose_runtime`、`face_runtime`、`expression_runtime` 都应显示 `backend=acl_om_npu`。如果 CANN/ACL 环境或 OM 文件不可用，生产路径会直接报错，避免静默回退到 CPU/PT/PTH。

### `character_voice_chat`

这是角色语音对话实验模块：

- `asr/`：Whisper/豆包 ASR 适配和假 ASR。
- `llm/`：角色 prompt 与模型调用。
- `rag/`：角色知识库检索。
- `core/`：对话管理、回复守卫和实时流水线。
- `audio/`：麦克风、VAD、音频缓冲和播放器。
- `output/`：控制台 speaker 和豆包 TTS speaker。

当前状态是“模块化实验”，还没有和 `smart_ip_interaction/backend/server.py` 做完整闭环集成。详细说明见 `character_voice_chat/README.md`。

## 环境准备

建议从仓库根目录准备通信模块和 Python 环境。

### 编译 UDP 视频流 Python 模块

`smart_ip_interaction/run_pipeline.py` 默认使用 UDP 视频流时，需要 `code/communication` 生成的 `video_stream_interface` 模块：

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/communication
cmake -S . -B build
cmake --build build -j
export PYTHONPATH="$(pwd)/build:${PYTHONPATH}"
```

### 加载 Ascend/CANN 环境

如需在 Ascend/CANN 环境下运行，先按现场路径确认脚本内容，再执行：

```bash
source /home/bb/competition_ws/ciciec_ascend/Scripts/ascend_310b/set_env.sh
```

### Python 依赖

视觉互动主链路常用依赖：

```bash
pip install opencv-python numpy pillow
pip install fastapi uvicorn websockets requests
```

只有本地调试 PT/PTH 回退或运行训练脚本时，才需要额外安装：

```bash
pip install ultralytics torch torchvision
```

语音对话模块会额外依赖麦克风、音频播放、WebSocket 或第三方 ASR/TTS SDK 相关库，当前 `character_voice_chat/requirements.txt` 仍为空，使用前需要按实际脚本补齐依赖。

## 运行视觉互动主链路

一键启动：

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/ascend_310B/smart_ip_interaction
python scripts/launch_pipeline.py
```

分步启动：

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/ascend_310B/smart_ip_interaction
python backend/server.py
```

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/ascend_310B/smart_ip_interaction
python scripts/video_player.py
```

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/ascend_310B/smart_ip_interaction
python run_pipeline.py
```

状态检查：

```bash
curl http://127.0.0.1:8000/status
```

本地模拟测试：

```bash
python scripts/launch_local_test.py
python scripts/simulate_310b.py --mode interactive
python scripts/fake_frontend_client.py
```

## 关键配置

### 推理视频源与后端地址

位置：`smart_ip_interaction/run_pipeline.py`

```python
USE_UDP_STREAM = True
UDP_LISTEN_PORT = 9090
UDP_MODULE_PATH = "/root/userdata/ciciec/communication/build"

BACKEND_HOST = "192.168.137.1"
BACKEND_PORT = 9000
```

- `USE_UDP_STREAM = True`：接收 PYNQ 发来的 UDP 视频流。
- `USE_UDP_STREAM = False`：使用本地摄像头或视频文件。
- `BACKEND_HOST` 需要指向 PC 后端所在地址。
- `UDP_MODULE_PATH` 或 `PYTHONPATH` 需要指向 `code/communication/build`。

### 视频播放配置

位置：`smart_ip_interaction/configs/video_config.json`

该配置控制后端指令到本地视频文件的映射。视频路径相对 `smart_ip_interaction/` 目录。

当前仓库已包含：

- `datasets/interaction_videos/idle/idle.mp4`
- `datasets/interaction_videos/wave/wave.mp4`
- `datasets/interaction_videos/wave/wave2.mp4`
- `datasets/interaction_videos/heart/heart.mp4`

`cheer` 和表情回应素材可按 `video_config.json` 继续补齐。

## 模型文件

姿态识别：

- 310B 生产首选：`smart_ip_interaction/models/om/yolo26s-pose.om`
- 本地调试 ONNX：`smart_ip_interaction/models/onnx/yolo26s-pose.onnx`
- 本地调试 PT：`smart_ip_interaction/yolo26n-pose.pt`

表情识别：

- 人脸检测生产首选：`smart_ip_interaction/models/om/face_detection_yunet_2023mar_640.om`
- 表情分类生产首选：`smart_ip_interaction/models/om/emotion_mobilenetv3_6cls.om`
- 本地调试人脸 ONNX：`smart_ip_interaction/models/pretrained/face_detection_yunet_2023mar.onnx`
- 本地调试表情 ONNX：`smart_ip_interaction/models/onnx/emotion_mobilenetv3_6cls.onnx`
- 本地调试 PTH：`smart_ip_interaction/models/checkpoints/emotion_mobilenetv3_6cls_best.pth`

生产路径默认强制 `ACL + OM + NPU`。需要临时指定模型或只在本地打开调试回退时，可使用：

```bash
export SMART_IP_POSE_MODEL=/root/userdata/ciciec/ascend_310B/smart_ip_interaction/models/om/yolo26s-pose.om
export SMART_IP_FACE_MODEL=/root/userdata/ciciec/ascend_310B/smart_ip_interaction/models/om/face_detection_yunet_2023mar_640.om
export SMART_IP_EMOTION_MODEL=/root/userdata/ciciec/ascend_310B/smart_ip_interaction/models/om/emotion_mobilenetv3_6cls.om

# 仅本地调试使用
export SMART_IP_POSE_REQUIRE_NPU=0
export SMART_IP_POSE_ALLOW_PT_FALLBACK=1
export SMART_IP_FACE_REQUIRE_NPU=0
export SMART_IP_FACE_ALLOW_OPENCV_FALLBACK=1
export SMART_IP_EXPR_REQUIRE_NPU=0
export SMART_IP_EXPR_ALLOW_TORCH_FALLBACK=1
```

## 端口与协议

| 端口 | 方向 | 说明 |
|---|---|---|
| `9090` | PYNQ -> Ascend 310B | UDP 视频流，`run_pipeline.py` 监听 |
| `9000` | Ascend 310B -> PC 后端 | TCP JSON 推理事件 |
| `8000` | PC 后端 -> 播放器 | WebSocket 服务，路径 `/ws` |
| `5567` | 控制端 -> 播放器 | UDP 视频控制兼容通道 |

事件示例：

```json
{"type": "action", "value": "wave_hand", "keypoints": [[x, y]]}
{"type": "action", "value": "heart_particles", "keypoints": [[x, y]]}
{"type": "action", "value": "cheer_light_sound"}
{"type": "expression", "value": "smile_feedback", "face_bbox": [x1, y1, x2, y2]}
```

后端广播示例：

```json
{"scene": "dialogue", "character": "bear", "dialogue_id": "wave_response", "tts": "你好呀！欢迎来到熊乐园！"}
{"scene": "switch_scene", "scene_id": 1, "trigger": "cheer", "cheer_total": 3}
```

## 训练与导出入口

表情模型训练：

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/ascend_310B/smart_ip_interaction
python training/emotion/train_emotion_6cls.py
python training/emotion/export_emotion_onnx.py
```

动作规则测试：

```bash
python training/pose/test_action_rules.py
```

## 真实硬件压测入口

视觉互动主链路已按真实部署路径输出三端 JSONL 日志，并用汇总脚本生成可写入报告的量化指标：

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/ascend_310B/smart_ip_interaction
python scripts/benchmark/summarize_hardware_e2e.py \
  --pynq-log-dir /home/xilinx/jupyter_notebooks/ciciec/log \
  --ascend-log-dir /root/userdata/ciciec/ascend_310B/log \
  --pc-log-dir /home/bb/competition_ws/ciciec_ascend/log
```

动作和表情准确率需要配合 `scripts/benchmark/action_trials_template.csv`、`scripts/benchmark/expression_trials_template.csv` 的真实 trial 标注计算。

最新硬件快照见 `smart_ip_interaction/README.md`。截至 `log/e2e_20260504_155646` 的最后一次成功启动，姿态、人脸检测、表情分类均为 `acl_om_npu`，310B 有效处理约 11.17 FPS，姿态推理平均 45.65 ms，表情分支平均 24.09 ms，单帧总耗时平均 69.99 ms。该 run 目录包含多次追加启动，正式压测建议每次使用新的日志目录。

## 注意事项

- `smart_ip_interaction` 是当前比赛演示主线，优先维护这里的 README 和启动方式。
- 视觉推理生产路径默认要求 CANN/ACL 和 OM 文件可用；不要把 CPU/PT/PTH 回退作为现场默认路径。
- `character_voice_chat` 当前仍有包路径、依赖和密钥配置整理工作，直接运行前需要先处理导入路径和环境变量。
- 语音相关脚本不应把真实 API Key、Token 或 App ID 固化在源码中，建议统一改为环境变量读取。
- 真实硬件联调时，需要同时确认 PYNQ/310B/PC 三端 IP、端口和 `code/communication/include/udp_common.h` 一致。
- PYNQ 采集到 PC 播放器切换的绝对端到端延迟依赖三端时钟同步；未同步时只使用 310B→后端、后端→播放器等同机/相邻分段延迟。
