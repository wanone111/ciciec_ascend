# 集创赛 华强 x 昇腾赛题 — 智能 IP 交互系统

![C++](https://img.shields.io/badge/C++-68.7%25-blue) ![Python](https://img.shields.io/badge/Python-31.3%25-yellow) 

## 项目概述

基于 **PYNQ-Z2 (FPGA)** + **Ascend 310B (NPU)** 构建异构视觉交互终端，面向园区/展演场景实现实时动作识别、表情捕捉与 IP 形象联动演艺。

### 核心功能

- ✅ **姿态识别**：挥手/比心/欢呼动作检测（YOLOv26s-Pose）
- ✅ **表情识别**：高兴/惊讶/生气/恐惧/悲伤/中性六分类（YuNet + MobileNetV3）
- ✅ **IP 视频联动**：熊二卡通形象互动视频播放
- ✅ **前后端通信**：UDP 视频流 + TCP 事件推送 + WebSocket 控制
- 🔄 **角色语音对话**：ASR + LLM + TTS 全流程（开发中）

---

## 系统架构

```
┌─────────────┐     UDP      ┌─────────────┐    TCP     ┌─────────────┐
│  PYNQ-Z2    │  视频流 9090  │ Ascend 310B │   :9000    │    PC 端    │
│  视频采集   │ ────────────> │  推理引擎   │ ────────>  │  后端服务   │
│  ISP 预处理 │               │ 姿态 + 表情  │            │ WebSocket   │
└─────────────┘               └─────────────┘            │    :8000    │
                                                         └──────┬──────┘
                                                                │
                                                                ▼
                                                         ┌─────────────┐
                                                         │  视频播放器 │
                                                         │  熊二互动   │
                                                         └─────────────┘
```

### 数据流向

1. **PYNQ-Z2**：摄像头采集 → ISP 预处理 → ROI 裁剪 → UDP 发送
2. **Ascend 310B**：接收 UDP 流 → YOLO-Pose 姿态推理 → 人脸检测 → 表情分类 → TCP 推送事件
3. **PC 后端**：接收事件 → 剧情状态机决策 → WebSocket 广播 → 控制视频播放

---

## 目录结构

```
/workspace/
├── code/
│   ├── ascend_310B/
│   │   ├── character_voice_chat/     # 角色语音对话系统
│   │   │   ├── asr/                  # 语音识别 (Whisper/豆包 ASR)
│   │   │   ├── llm/                  # 大语言模型 (豆包 API)
│   │   │   ├── output/               # TTS 语音合成 (豆包 TTS)
│   │   │   ├── core/                 # 对话管理 + 实时流水线
│   │   │   ├── rag/                  # 简单知识库检索
│   │   │   ├── audio/                # 音频输入/输出处理
│   │   │   ├── main.py               # 文本对话入口
│   │   │   └── requirements.txt      # Python 依赖
│   │   │
│   │   └── smart_ip_interaction/     # ★ 智能 IP 互动系统
│   │       ├── run_pipeline.py       # 主推理入口
│   │       ├── backend/
│   │       │   └── server.py         # 事件分发 + 剧情状态机
│   │       ├── src/
│   │       │   ├── behavior/         # 动作规则 + 状态机
│   │       │   ├── inference/        # 姿态/表情/人脸推理
│   │       │   ├── interaction/      # 事件→指令映射
│   │       │   └── communication/    # TCP 发送 + UDP 视频源
│   │       ├── scripts/
│   │       │   ├── launch_pipeline.py    # 一键启动
│   │       │   ├── launch_local_test.py  # 模拟测试
│   │       │   └── video_player.py       # 视频播放器
│   │       ├── training/             # 模型训练脚本
│   │       └── datasets/             # 互动视频资产
│   │
│   └── communication/                # 三端 UDP 通信模块 (C++)
│       └── src/                      # pc_udp / 310B_udp / pynq_udp
│
├── Scripts/                          # 环境配置脚本
├── docs/                             # 技术文档
└── README.md                         # 本文件
```

---

## 快速开始

### 环境准备

```bash
# 激活 Conda 环境
conda activate 310B

# 安装 Python 依赖
pip install ultralytics opencv-python torch torchvision
pip install fastapi uvicorn websockets requests sounddevice soundfile websocket-client
```

### 方式一：一键启动（推荐）

在 Ascend 310B 或本地测试环境运行：

```bash
cd code/ascend_310B/smart_ip_interaction
python scripts/launch_pipeline.py
```

该脚本自动启动：
- `backend/server.py` — 后端事件分发服务
- `scripts/video_player.py` — 互动视频播放器
- `run_pipeline.py` — 姿态 + 表情推理引擎

### 方式二：分步启动

#### 1. 启动后端服务

```bash
cd code/ascend_310B/smart_ip_interaction
python backend/server.py
```

验证后端状态：
```bash
curl http://127.0.0.1:8000/status
```

#### 2. 启动视频播放器

```bash
# 清除代理环境变量（重要）
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY

python scripts/video_player.py
```

#### 3. 启动推理引擎

```bash
python run_pipeline.py
```

---

## 配置说明

### 视频源选择 (`run_pipeline.py`)

```python
# 使用硬件 UDP 视频流（PYNQ → 310B）
USE_UDP_STREAM = True
UDP_LISTEN_PORT = 9090

# 或使用本地摄像头/视频文件
USE_UDP_STREAM = False
VIDEO_SOURCE = 0              # 0 = 摄像头
VIDEO_SOURCE = "video.mp4"    # 或视频文件路径
```

### 后端地址

```python
BACKEND_HOST = "192.168.137.1"
BACKEND_PORT = 9000
```

### 互动视频配置

编辑 `configs/video_config.json` 修改动作→视频映射，无需改代码。

视频文件放置位置：
```
datasets/interaction_videos/
├── idle/          # 待机循环
├── wave/          # 挥手回应
├── heart/         # 比心回应
├── cheer/         # 欢呼回应
└── expressions/   # 表情回应
    ├── happy/
    ├── surprised/
    ├── angry/
    ├── fear/
    └── sad/
```

---

## 算法模块

### 姿态识别

| 组件 | 模型 | 输入尺寸 |
|------|------|----------|
| 姿态估计 | YOLOv26s-Pose | 640x640 |
| 动作分类 | 规则引擎 | COCO 17 关键点 |

**动作判定规则**：
| 动作 | 触发条件 |
|------|----------|
| 挥手 (WAVE) | 任一手腕高于同侧肩膀 |
| 比心 (HEART) | 双腕靠拢且位于胸前区域 |
| 欢呼 (CHEER) | 双手同时高于头部 |

### 表情识别

| 组件 | 模型 | 类别数 |
|------|------|--------|
| 人脸检测 | YuNet (ONNX) | - |
| 表情分类 | MobileNetV3-Small | 6 类 |

**表情类别**：happy(高兴), surprised(惊讶), angry(生气), fear(恐惧), sad(悲伤), neutral(中性)

**平滑策略**：7 帧滑动窗口多数投票

---

## 通信协议

### TCP 事件格式 (310B → PC)

```json
{"type": "action", "value": "wave_hand", "keypoints": [[x1,y1], ...]}
{"type": "action", "value": "heart_particles", "face_bbox": [x1,y1,x2,y2]}
{"type": "expression", "value": "smile_feedback", "face_bbox": [x1,y1,x2,y2]}
```

### WebSocket 控制格式 (后端 → 播放器)

```json
{"scene": "dialogue", "character": "bear", "dialogue_id": "wave_response", "tts": "你好呀！"}
{"scene": "switch_scene", "scene_id": 1, "trigger": "cheer", "cheer_total": 3}
{"scene": "ar_overlay", "effect": "bear_hat", "keypoints": [[x1,y1], ...]}
```

---

## 剧情状态机

后端内置简单剧情逻辑 (`backend/server.py`)：

- **欢呼累计**：每 3 次 CHEER 触发剧情线切换（共 3 条剧情线循环）
- **AR 合影**：HEART 动作附带关键点实时推送，用于 AR 道具叠加
- **冷却机制**：所有事件 2 秒冷却时间，防止重复触发

修改阈值：
```python
CHEER_SCENE_THRESHOLD = 3  # 欢呼累计次数触发剧情切换
SCENE_COUNT = 3            # 总剧情线数量
```

---

## 角色语音对话（开发中）

基于豆包 API 实现的熊二角色扮演对话系统。

### 模块组成

| 模块 | 实现 | 说明 |
|------|------|------|
| ASR | Whisper / 豆包 ASR | 语音转文字 |
| LLM | 豆包 Seed 2.0 Pro | 角色扮演对话 |
| TTS | 豆包 TTS | 文字转语音（熊二音色） |
| RAG | 简单关键词检索 | 角色知识库增强 |

### 快速测试

```bash
cd code/ascend_310B/character_voice_chat
python main.py
```

进入交互式文本对话模式，输入 `exit` 或 `quit` 退出。

### 配置文件

- `llm/prompts.py`：角色提示词模板
- `core/dialogue_manager.py`：对话管理逻辑
- `output/doubao_tts_speaker.py`：TTS 参数配置

---

## 网络参数（默认）

来源：`code/communication/include/udp_common.h`

| 设备 | IP 地址 | 说明 |
|------|---------|------|
| PC | 192.168.137.1 | 后端服务 + 播放器 |
| 310B (eth0) | 192.168.2.2 | 接收 PYNQ 视频流 |
| 310B (eth1) | 192.168.137.100 | 与 PC 通信 |
| PYNQ | 192.168.2.99 | 视频采集端 |

| 端口 | 用途 |
|------|------|
| 9090 | PYNQ → 310B 视频流 |
| 9000 | 310B → PC TCP 事件推送 |
| 8000 | 后端 → 播放器 WebSocket |

---

## 调试工具

### 模拟测试（无需硬件）

```bash
# 使用模拟数据验证完整链路
python scripts/launch_local_test.py

# 手动发送单条事件测试视频切换
python scripts/simulate_310b.py --mode interactive

# WebSocket 调试客户端
python scripts/fake_frontend_client.py
```

### 日志查看

```bash
# 后端状态查询
curl http://127.0.0.1:8000/status

# 推理统计（每 10 秒打印）
# 运行 run_pipeline.py 时自动输出
```

---

## 模型训练

### 表情模型训练

```bash
cd code/ascend_310B/smart_ip_interaction

# 训练 6 分类表情模型
python training/emotion/train_emotion_6cls.py

# 导出 ONNX 格式
python training/emotion/export_emotion_onnx.py
```

训练数据位于 `datasets/emotion_dataset_6cls/`。

---

## 已知问题与技术债务

- ⚠️ **AIPP 硬件预处理未实现**：当前使用 OpenCV DNN + CANN/NPU 后端进行软件预处理，未使用昇腾 AIPP 硬件加速模块。如需部署到生产环境，建议根据实际模型格式创建 `.aipp` 配置文件并集成 DVPP+AIPP 流水线
- ⚠️ 错误处理与日志系统需完善
- ⚠️ 缺少单元测试与集成测试框架
- ⚠️ 角色语音对话尚未与 IP 互动系统集成

---

## 里程碑状态

| 里程碑 | 状态 | 说明 |
|--------|------|------|
| M1 通信链路打通 | ✅ | PYNQ → 310B → PC 三端 UDP 链路 |
| M2 姿态识别模块 | ✅ | YOLOv26s-Pose + 动作分类 |
| M3 表情识别模块 | ✅ | YuNet + MobileNetV3 六分类 |
| M4 IP 视频联动 | ✅ | 熊二互动视频播放 + 剧情状态机 |
| M5 角色语音对话 | 🔄 | ASR+LLM+TTS 端到端联调中 |
| M6 性能压测 | ⏳ | 待开始 |

---

## 相关文档

- 智能 IP 互动详细说明：`code/ascend_310B/smart_ip_interaction/README.md`
- 算法接口规范：`code/ascend_310B/smart_ip_interaction/configs/algorithm_interface_v1.0.md`
- 总体架构设计：`docs/报告.md`

---

## 许可证

本项目为集创赛参赛代码，仅供学习交流使用。

