# Smart IP Interaction — 智能 IP 互动系统

实时识别观众的**肢体动作**和**面部表情**，驱动熊二 IP 形象播放对应互动视频。

## 系统架构

```
摄像头 / UDP硬件流
       │
       ▼
 run_pipeline.py          ← 姿态 + 表情推理（YOLO-Pose + MobileNetV3）
       │ TCP :9000
       ▼
 backend/server.py         ← 事件分发、剧情状态机
       │ WebSocket :8000
       ▼
 video_player.py           ← 熊二互动视频播放（播完自动回待机）
```

识别动作 → 后端映射 → 切换播放对应熊二视频，播完自动回待机循环。

---

## 目录结构

```
smart_ip_interaction/
├── run_pipeline.py                  # 主推理入口
├── backend/
│   └── server.py                    # 后端事件分发服务
├── configs/
│   ├── video_config.json            # 动作→视频路径映射（改这里换视频）
│   └── algorithm_interface_v1.0.md  # 算法接口文档
├── datasets/
│   ├── action_videos/               # 动作识别测试视频
│   ├── emotion_dataset_6cls/        # 表情分类训练数据集
│   └── interaction_videos/          # ★ 熊二互动视频（放在这里）
│       ├── idle/                    # 待机循环视频
│       ├── wave/                    # 挥手回应
│       ├── heart/                   # 比心回应
│       ├── cheer/                   # 欢呼回应
│       └── expressions/
│           ├── happy/
│           ├── surprised/
│           ├── angry/
│           ├── fear/
│           └── sad/
├── models/
│   ├── checkpoints/
│   │   └── emotion_mobilenetv3_6cls_best.pth
│   └── pretrained/
│       └── face_detection_yunet_2023mar.onnx
├── scripts/
│   ├── launch_pipeline.py           # ★ 一键启动全流程
│   ├── launch_local_test.py         # 联调测试（用模拟数据）
│   ├── simulate_310b.py             # 模拟边缘端发送事件
│   ├── video_player.py              # 视频播放程序
│   └── fake_frontend_client.py      # WebSocket 调试客户端
├── src/
│   ├── behavior/                    # 动作判断规则 + 状态机
│   ├── inference/                   # 表情推理 + 人脸检测 + AR 道具
│   ├── interaction/                 # 事件→指令映射
│   └── communication/              # TCP 发送 + UDP 视频流适配器
├── training/
│   ├── emotion/                     # 表情模型训练 / 导出脚本
│   └── pose/                        # 动作规则调试脚本
└── yolo26n-pose.pt                  # YOLO-Pose 姿态模型
```

---

## 环境依赖

```bash
conda activate torch_env

pip install ultralytics opencv-python torch torchvision
pip install fastapi uvicorn websockets
```

如使用硬件 UDP 视频流（PYNQ -> 310B），还需要先编译通信模块，生成 Python 侧的
`video_stream_interface` 动态库：

```bash
cd ../communication
cmake -S . -B build
cmake --build build -j

# 让 smart_ip_interaction 可以导入 video_stream_interface
export PYTHONPATH=$(pwd)/build:$PYTHONPATH
```

NPU 运行说明：
- 姿态推理优先使用 `models/onnx/yolo26s-pose.onnx`，如果板端 OpenCV 编译了 CANN 后端，会自动切到 NPU。
- 表情推理优先使用 `models/onnx/emotion_mobilenetv3_6cls.onnx`。
- 如果当前环境没有 CANN 后端，代码会自动回退到 CPU，便于先把整条链路跑通。

---

## 快速开始

### 第一步：准备熊二互动视频

将视频文件按以下命名放入对应目录：

| 目录 | 文件名 | 触发条件 |
|---|---|---|
| `datasets/interaction_videos/idle/` | `idle.mp4` | 默认待机循环 |
| `datasets/interaction_videos/wave/` | `wave.mp4` | 挥手 |
| `datasets/interaction_videos/heart/` | `heart.mp4` | 比心 |
| `datasets/interaction_videos/cheer/` | `cheer.mp4` | 欢呼 |
| `datasets/interaction_videos/expressions/happy/` | `happy.mp4` | 表情：高兴 |
| `datasets/interaction_videos/expressions/surprised/` | `surprised.mp4` | 表情：惊讶 |
| `datasets/interaction_videos/expressions/angry/` | `angry.mp4` | 表情：生气 |
| `datasets/interaction_videos/expressions/fear/` | `fear.mp4` | 表情：恐惧 |
| `datasets/interaction_videos/expressions/sad/` | `sad.mp4` | 表情：悲伤 |

> 视频路径也可在 `configs/video_config.json` 中自由修改，无需改代码。

### 第二步：启动 PC 端（后端 + 播放器）

```bash
cd smart_ip_interaction
python backend/server.py
# 新开终端
python scripts/video_player.py
```

可用以下命令确认后端正常：

```bash
curl http://127.0.0.1:8000/status
```

### 第三步：启动 310B 推理端

```bash
cd smart_ip_interaction
python run_pipeline.py
```

### 第四步：启动 PYNQ 发送端

PYNQ 端启动采集/预处理与 UDP 发送程序，目标地址应发送到 310B 的 `PYNQ_PORT`（默认 `9090`）。

### 可选：一键启动（同机联调）

```bash
cd smart_ip_interaction
python scripts/launch_pipeline.py
```

该脚本会启动：
- backend/server.py
- scripts/video_player.py
- run_pipeline.py

识别到动作后自动切换互动视频，播完回待机。

---

## 视频源配置

编辑 `run_pipeline.py` 顶部配置：

```python
# 默认：使用硬件 UDP 视频流（PYNQ -> 310B）
USE_UDP_STREAM = True
UDP_LISTEN_PORT = 9090
UDP_MODULE_PATH = None  # 用 PYTHONPATH 查找 video_stream_interface

# 推理事件发送到 PC 后端（TCP 9000）
BACKEND_HOST = "192.168.137.1"
BACKEND_PORT = 9000

# 如果改为本地摄像头/视频文件联调：
# USE_UDP_STREAM = False
# VIDEO_SOURCE = 0

# 使用视频文件
USE_UDP_STREAM = False
VIDEO_SOURCE = r"path/to/video.mp4"
```

注意：如果不设置 `PYTHONPATH`，可手动指定模块路径：

```python
UDP_MODULE_PATH = "/absolute/path/to/ciciec_ascend/code/communication/build"
```

---

## 三端网络参数（默认）

来源：`code/communication/include/udp_common.h`

| 设备 | IP | 说明 |
|---|---|---|
| PC | 192.168.137.1 | 后端与播放端 |
| 310B (eth0) | 192.168.2.2 | 接收 PYNQ 视频流 |
| 310B (eth1) | 192.168.137.100 | 与 PC 通信 |
| PYNQ | 192.168.2.99 | 视频采集与预处理 |

| 端口 | 说明 |
|---|---|
| 9090 | PYNQ -> 310B 视频流接收端口 (`PYNQ_PORT`) |
| 9000 | 310B -> PC 后端 TCP 推理事件端口 |
| 8000 | 后端 -> 播放器 WebSocket 端口 |

如现场 IP 不同，请同时修改：
- `code/communication/include/udp_common.h`
- `smart_ip_interaction/run_pipeline.py` 中 `BACKEND_HOST`

---

## 动作识别说明

基于 COCO 17 点骨骼关键点，单帧规则判断：

| 动作 | 触发条件 |
|---|---|
| 挥手 (WAVE) | 任一手腕高于同侧肩膀 |
| 比心 (HEART) | 双腕靠拢且位于胸前区域 |
| 欢呼 (CHEER) | 双手同时高于头部 |

动作需连续 **2 帧**触发，同一动作冷却 **0.8 秒**，优先级 CHEER > HEART > WAVE。

表情识别使用 **YuNet 人脸检测 + MobileNetV3-Small 6 分类**，7 帧滑动窗口多数投票平滑。

---

## 调试工具

```bash
# 不启动摄像头，用模拟数据验证完整链路
python scripts/launch_local_test.py

# 手动发送单条事件（测试视频切换）
python scripts/simulate_310b.py --mode interactive

# 查看后端实时状态
curl http://127.0.0.1:8000/status
```

---

## 剧情状态机

`backend/server.py` 内置简单剧情逻辑：
- **CHEER 累计 3 次** → 触发剧情切换（`switch_scene`）
- **HEART** → 同时触发 AR 合影特效（关键点实时推送）
- 所有事件有 **2 秒冷却**，防止重复触发

修改 `server.py` 中的 `CHEER_SCENE_THRESHOLD` 可调整切换阈值。

---

## 模型训练

如需重新训练表情模型：

```bash
# 训练（数据集在 datasets/emotion_dataset_6cls/）
python training/emotion/train_emotion_6cls.py

# 导出 ONNX
python training/emotion/export_emotion_onnx.py
```
