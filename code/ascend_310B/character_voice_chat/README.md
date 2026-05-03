# character_voice_chat 角色语音对话模块

本目录是熊二角色语音对话的实验模块，目标是把 ASR、LLM、RAG、TTS 和实时音频流处理组合成可打断的语音对话流水线。当前它尚未完整接入 `../smart_ip_interaction/` 的动作/表情事件链路。

## 当前状态

- 已有文本对话管理器、角色知识库检索和回复守卫。
- 已有假音频流、假 ASR、控制台 speaker 等本地测试组件。
- 已有豆包 ASR/TTS 适配代码和 LLM 调用代码。
- `config.py`、`app.py` 和 `requirements.txt` 当前为空，尚未形成稳定安装入口。
- 多个脚本使用 `ciciec_ascend.character_voice_chat.*` 包路径；独立运行前需要先整理 Python 包路径或调整 import。

## 目录结构

```text
character_voice_chat/
├── README.md
├── main.py
├── app.py
├── config.py
├── requirements.txt
├── asr/
├── audio/
├── core/
├── data/
│   └── character_kb/
├── llm/
├── output/
├── rag/
├── test_adapter.py
├── test_headset_mic.py
├── test_mic_to_pipeline.py
└── test_stream_pipeline.py
```

## 模块职责

| 模块 | 说明 |
|---|---|
| `asr/` | ASR 抽象类、假 ASR、Whisper/豆包 ASR 适配 |
| `audio/` | 麦克风输入、VAD、录音、音频缓冲、播放等 |
| `core/` | 对话管理、实时流水线、状态机、路由和回复守卫 |
| `data/character_kb/` | 熊二角色资料、关系、说话风格和规则 |
| `llm/` | 角色 prompt 构造和模型调用 |
| `output/` | 控制台 speaker 和豆包 TTS speaker |
| `rag/` | 简单关键词知识库检索 |

## 本地测试入口

假音频流 + 假 ASR + 控制台输出：

```bash
python test_stream_pipeline.py
```

麦克风到 ASR/LLM/TTS 流水线：

```bash
python test_mic_to_pipeline.py
```

文本对话入口：

```bash
python main.py
```

注意：上述脚本当前依赖 `ciciec_ascend.character_voice_chat` 形式的包路径。如果直接在本目录运行失败，需要先将模块整理为可导入包，或临时设置/调整 `PYTHONPATH` 与 import 路径。

## 配置与密钥

当前部分测试脚本和模型调用代码中存在本地调试用的服务配置。实际提交、部署或演示前应改为从环境变量或独立配置文件读取，并避免把真实密钥写入源码。

建议约定：

```bash
export DOUBAO_APP_ID="..."
export DOUBAO_TOKEN="..."
export VOLCENGINE_API_KEY="..."
```

然后在 `config.py` 中统一读取这些环境变量，供 ASR、LLM 和 TTS 模块使用。

## 与视觉互动系统集成的建议

后续可以把本模块作为 `smart_ip_interaction/backend/server.py` 的可选能力：

- 动作事件 `wave_hand`、`heart_particles` 触发固定或 LLM 生成回复。
- 表情事件触发带情绪的角色回应。
- TTS 输出可同步给播放器、前端或音频设备。
- 使用 `core/realtime_pipeline.py` 支持用户插话打断。

集成前应先完成：

- 统一包路径和启动方式。
- 统一配置/密钥读取。
- 为 ASR、LLM、TTS 调用增加超时、重试和错误兜底。
- 将输出消息格式对齐 `smart_ip_interaction` 的 WebSocket 控制协议。
