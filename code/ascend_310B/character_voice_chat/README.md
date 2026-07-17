# character_voice_chat 角色语音交互

这个包负责把音频输入、端点检测、ASR、角色 RAG/LLM 和 TTS 串成一条语音对话流水线。目前支持无密钥离线自检，以及豆包 ASR + 方舟 LLM + 豆包 TTS 的真实麦克风模式。

## 已完成

- 包路径和模块启动方式已统一，可从 `ascend_310B` 目录用 `python -m` 启动。
- 保留用户指定的源码硬编码凭据；同名环境变量可临时覆盖方舟 LLM 凭据。
- 离线测试不会调用云端服务。
- 端点检测会忽略开口前底噪，并限制单句最大缓存长度。
- 空 ASR 结果不会进入 LLM/TTS。
- 对话历史有长度上限，避免 prompt 无限增长。
- ASR 超时会主动关闭 WebSocket。
- TTS 支持代次取消：被打断的旧合成请求返回后不会重新播放。
- 真实麦克风演示默认采用半双工，避免扬声器回声被识别成用户新问题。

## 快速自检

在本文件上一级的 `ascend_310B` 目录执行：

```bash
python3 -m unittest discover -s character_voice_chat/tests -v
python3 -m character_voice_chat.test_stream_pipeline
```

离线文本对话：

```bash
python3 -m character_voice_chat.main
```

在线文本对话（只使用 LLM）：

```bash
export VOLCENGINE_API_KEY="你的密钥"
python3 -m character_voice_chat.main --online
```

## 真实麦克风对话

安装依赖：

```bash
python3 -m pip install -r character_voice_chat/requirements.txt
```

源码已保留现有凭据，可直接启动：

```bash
python3 -m character_voice_chat.test_mic_to_pipeline
```

程序会自动等待开口、检测尾静音、识别、生成角色回复并播放语音。为了避免声学回声，当前默认等 TTS 播放完成后再开启下一轮监听；使用耳机时可在后续全双工入口中启用插话打断。

指定麦克风并显示实时输入电平：

```bash
python3 -m character_voice_chat.test_mic_to_pipeline \
  --input-device 4 \
  --debug-audio
```

程序会先丢弃 0.5 秒设备启动瞬态，再进行 0.8 秒环境噪声校准，此时请保持安静。可用
`--list-devices` 查看设备；仍无法触发时可手动传入
`--speech-threshold 0.002 --silence-threshold 0.001`。

对于只支持 44.1/48 kHz 的 ALSA 硬件设备，程序会自动按设备原生采样率采集，
再重采样到 ASR 所需的 16 kHz；也可用 `--capture-sample-rate 48000` 手动指定。

## 配置

参考 `environment.example`。常用参数：

| 环境变量 | 默认值 | 作用 |
|---|---:|---|
| `VOICE_SAMPLE_RATE` | `16000` | 麦克风/ASR 采样率 |
| `VOICE_SPEECH_START_THRESHOLD` | `0.01` | 开口能量阈值 |
| `VOICE_SILENCE_THRESHOLD` | `0.003` | 尾静音阈值 |
| `VOICE_SILENCE_CHUNKS` | `4` | 判定句末所需静音块数 |
| `VOICE_MAX_UTTERANCE_SECONDS` | `30` | 单句最大缓存时长 |
| `VOLCENGINE_MODEL` | `doubao-seed-2-0-pro-260215` | 在线对话模型 |

阈值需要按麦克风增益和环境噪声调整，并始终保持静音阈值小于开口阈值。

## 流水线

```text
麦克风/音频块
  -> 格式归一化
  -> 开口与尾静音检测
  -> ASR
  -> 角色路由 + 知识库检索 + LLM
  -> {text, emotion, action}
  -> TTS / 控制台 / 后续前端事件
```

`app.py` 提供统一工厂，后续可由 `smart_ip_interaction` 后端复用，不必在 WebSocket 服务里重复组装依赖。

## 当前边界与下一步

- 真实麦克风入口仍是稳定优先的半双工模式；真正全双工需要耳机或 AEC，并把 ASR/LLM/TTS 移到后台任务队列。
- 当前 VAD 是能量阈值法；嘈杂环境建议接入 Silero VAD 或 WebRTC VAD，并增加 200–300 ms 预卷缓冲。
- 云端 ASR/TTS 尚需补请求级延迟指标、重试/熔断和错误码分类。
- 尚未把 `emotion/action` 实时推送给右侧动漫前端；可接入 `smart_ip_interaction` 的 WebSocket 事件协议。
