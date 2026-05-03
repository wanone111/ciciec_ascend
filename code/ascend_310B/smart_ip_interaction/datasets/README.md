# datasets 目录说明

本目录存放智能 IP 互动系统需要的演示素材和可选训练数据。当前仓库主要包含互动视频素材，训练数据集目录可按需要自行补齐。

## 当前结构

```text
datasets/
├── README.md
└── interaction_videos/
    ├── idle/
    │   └── idle.mp4
    ├── wave/
    │   ├── wave.mp4
    │   └── wave2.mp4
    ├── heart/
    │   └── heart.mp4
    └── cheer/
```

`scripts/video_player.py` 会读取 `../configs/video_config.json` 中的相对路径，根据后端 WebSocket 指令切换视频。

## 视频映射

默认配置文件：`../configs/video_config.json`

| 指令 key | 默认路径 | 状态 |
|---|---|---|
| `idle` | `interaction_videos/idle/idle.mp4` | 已包含 |
| `wave_response` | `interaction_videos/wave/wave.mp4` | 已包含 |
| `heart_response` | `interaction_videos/heart/heart.mp4` | 已包含 |
| `cheer_effect` | `interaction_videos/cheer/cheer.mp4` | 需补齐 |
| `switch_scene` | `interaction_videos/cheer/cheer.mp4` | 需补齐 |
| `smile_response` | `interaction_videos/expressions/happy/happy.mp4` | 需补齐 |
| `wow_response` | `interaction_videos/expressions/surprised/surprised.mp4` | 需补齐 |
| `angry_response` | `interaction_videos/expressions/angry/angry.mp4` | 需补齐 |
| `fear_response` | `interaction_videos/expressions/fear/fear.mp4` | 需补齐 |
| `sad_response` | `interaction_videos/expressions/sad/sad.mp4` | 需补齐 |

如果某个视频缺失，播放器会输出告警并忽略对应指令，不会中断主程序。

## 添加素材

按配置路径放入视频，或直接修改 `../configs/video_config.json` 指向新的相对路径。建议使用常见 MP4/H.264 编码，便于 OpenCV `VideoCapture` 稳定读取。

## 训练数据

表情训练脚本位于 `../training/emotion/`，如果需要重新训练 6 类表情模型，可在本目录下新增类似 `emotion_dataset_6cls/` 的 ImageFolder 数据集，并保持类别目录与训练脚本的类别顺序一致：

```text
emotion_dataset_6cls/
├── angry/
├── fear/
├── happy/
├── neutral/
├── sad/
└── surprised/
```
