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

### 第二步：一键启动

```bash
cd smart_ip_interaction
python scripts/launch_pipeline.py
```

启动后弹出三个窗口：
- **推理窗口**：显示姿态骨骼 + 表情标签 + FPS
- **视频窗口**：熊二互动视频（默认循环待机）
- **后端窗口**：事件日志

识别到动作后自动切换互动视频，播完回待机。

---

## 视频源配置

编辑 `run_pipeline.py` 顶部配置：

```python
# 使用本地摄像头（默认）
USE_UDP_STREAM = False
VIDEO_SOURCE = 0

# 使用视频文件
USE_UDP_STREAM = False
VIDEO_SOURCE = r"path/to/video.mp4"

# 使用硬件 UDP 视频流（PYNQ → 310B → PC）
USE_UDP_STREAM = True
UDP_LISTEN_PORT = 5555
UDP_MODULE_PATH = "/path/to/build"   # video_stream_interface.so 所在目录
```

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
