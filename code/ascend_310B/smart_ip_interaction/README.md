# Smart IP Interaction 智能 IP 互动系统

> 2026-07-18: the pipeline now has opt-in `cpu`, `aipp` and `dvpp_aipp`
> preprocessing backends. Defaults remain the legacy CPU/float32 path. The
> current verified source is the Hikrobot GigE camera; see
> `../../../docs/DVPP_AIPP_INTEGRATION_REPORT.md` for contracts, rollback,
> board results and launch commands. PYNQ raw NV12 is prepared but was not
> hardware-connected in this validation run. The default pose model is now the
> contract-matched `yolo26n-pose-640.om`; its packed-BGR AIPP and experimental
> DVPP+NV12-AIPP variants passed board A/B checks. Packed-BGR AIPP is the
> recommended Hik path. Face AIPP remains opt-in pending model-specific parity.

本目录是 Ascend 310B 侧的智能视觉交互主链路。它接收本地摄像头、视频文件或 PYNQ 发来的 UDP 视频流，完成姿态识别、动作规则判断、人脸检测和 6 类表情识别，然后通过 TCP 将互动事件发送到 PC 后端，由后端驱动熊二 IP 视频播放器。

## 运行链路

```text
PYNQ UDP 视频流 / 本地摄像头 / 视频文件
        |
        v
run_pipeline.py
  - PoseInfer: YOLO-Pose 姿态关键点
  - ActionStateMachine: WAVE / HEART / CHEER
  - FaceDetector: YuNet 人脸检测
  - ExpressionInfer: MobileNetV3 6 类表情
        |
        | TCP JSON :9000
        v
backend/server.py
  - 剧情状态机
  - WebSocket /ws :8000
        |
        v
scripts/video_player.py
  - 根据 configs/video_config.json 切换互动视频
```

## 目录说明

```text
smart_ip_interaction/
├── run_pipeline.py
├── backend/
│   └── server.py
├── configs/
│   ├── algorithm_interface_v1.0.md
│   └── video_config.json
├── datasets/
│   ├── README.md
│   └── interaction_videos/
├── models/
│   ├── checkpoints/
│   ├── om/
│   ├── onnx/
│   └── pretrained/
├── scripts/
│   ├── fake_frontend_client.py
│   ├── launch_local_test.py
│   ├── launch_pipeline.py
│   ├── simulate_310b.py
│   └── video_player.py
├── src/
│   ├── behavior/
│   ├── communication/
│   ├── inference/
│   └── interaction/
├── training/
│   ├── emotion/
│   └── pose/
└── yolo26n-pose.pt
```

## 环境准备

建议在仓库根目录先编译通信模块，让 UDP 视频流适配器可以导入 `video_stream_interface`：

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/communication
cmake -S . -B build
cmake --build build -j
export PYTHONPATH="$(pwd)/build:${PYTHONPATH}"
```

Python 依赖按实际环境安装：

```bash
pip install opencv-python numpy pillow
pip install fastapi uvicorn websockets requests
```

310B 生产推理路径默认使用 `models/om/*.om`，需要先加载 CANN/ACL 环境：

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh
# 或现场封装脚本：
source /home/bb/competition_ws/ciciec_ascend/Scripts/ascend_310b/set_env.sh
```

只有本地调试 PT/PTH 回退或训练脚本才需要额外安装 `ultralytics torch torchvision`。

## 快速启动

一键启动后端、播放器和推理进程：

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/ascend_310B/smart_ip_interaction
python scripts/launch_pipeline.py
```

分步启动时，使用三个终端：

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

后端状态检查：

```bash
curl http://127.0.0.1:8000/status
```

## 本地联调

无 PYNQ/摄像头输入时，可以只验证后端和播放器：

```bash
python scripts/launch_local_test.py
```

手动发送事件：

```bash
python scripts/simulate_310b.py --mode interactive
```

WebSocket 调试客户端：

```bash
python scripts/fake_frontend_client.py
```

## 真实硬件端到端压测

默认按三端真实路径写出压测日志：PYNQ 写 `/home/xilinx/jupyter_notebooks/ciciec/log/pynq.jsonl`，310B 写 `/root/userdata/ciciec/ascend_310B/log/pipeline.jsonl`，PC 写 `/home/bb/competition_ws/ciciec_ascend/log/backend.jsonl` 和 `player.jsonl`。如需临时覆盖，可设置 `SMART_IP_PYNQ_BENCH_DIR`、`SMART_IP_310B_BENCH_DIR`、`SMART_IP_PC_BENCH_DIR` 或 `SMART_IP_BENCH_DIR`。

```bash
export SMART_IP_UDP_MODULE_PATH=/home/bb/competition_ws/ciciec_ascend/code/communication/build
export SMART_IP_BACKEND_HOST=192.168.137.1
export SMART_IP_USE_UDP=1
```

真实硬件运行顺序：

1. PC：`python backend/server.py`
2. PC：`python scripts/video_player.py`
3. 310B：`python run_pipeline.py`
4. PYNQ：`code/communication/build/pynq_udp`

汇总：

```bash
python scripts/benchmark/summarize_hardware_e2e.py \
  --pynq-log-dir /home/xilinx/jupyter_notebooks/ciciec/log \
  --ascend-log-dir /root/userdata/ciciec/ascend_310B/log \
  --pc-log-dir /home/bb/competition_ws/ciciec_ascend/log \
  --action-trials scripts/benchmark/action_trials_template.csv \
  --expression-trials scripts/benchmark/expression_trials_template.csv
```

输出的 `hardware_e2e_summary.md` 包含动作/表情准确率、端到端延迟、FPS、UDP 丢包率和稳定性统计。若未提供 trial CSV，脚本只汇总性能和稳定性，不计算准确率。

### 最新硬件压测快照

归档目录：

```text
/home/bb/competition_ws/ciciec_ascend/log/e2e_20260504_155646
```

该目录中存在多次启动追加日志，下面按最后一次成功 `pipeline_start` 重新截取统计：

| 指标 | 数值 |
|---|---|
| PYNQ 发送帧数 | 690 帧 |
| PYNQ 实际发送帧率 | 15.02 FPS |
| PYNQ 发送失败包 | 0 |
| 310B 处理帧数 | 498 帧 |
| 310B 有效处理帧率 | 11.17 FPS |
| 310B 汇报 FPS | 平均 11.60，P50 11.93，P95 14.87 |
| UDP 包/帧丢失率 | 0.00% / 0.00% |
| UDP 组帧平均延迟 | 80.77 ms |
| 源帧缺失率 | 13 / 511，约 2.54% |
| 姿态推理后端 | `acl_om_npu` |
| 人脸检测后端 | `acl_om_npu` |
| 表情分类后端 | `acl_om_npu` |
| 姿态推理耗时 | 平均 45.65 ms，P50 44.84 ms，P95 51.32 ms |
| 表情分支耗时 | 平均 24.09 ms，P50 23.10 ms，P95 31.95 ms |
| 单帧总耗时 | 平均 69.99 ms，P50 65.55 ms，P95 94.80 ms |
| 播放器实际切换 | 16 次，`heart_response` 14 次，`wave_response` 2 次 |

事件分布：动作事件 48 次，其中 `HEART` 42 次、`WAVE` 5 次、`CHEER` 1 次；表情事件 28 次，其中 `angry` 14 次、`neutral` 10 次、`surprised` 4 次。播放器 missing 26 次，主要来自 `ar_overlay`、`angry_response`、`wow_response`、`cheer_effect` 缺少素材或映射未补齐。

准确率注意事项：本次 action/expression trial CSV 仍是模板时间戳，汇总中的 0% 准确率不是模型真实准确率。正式报告需要按现场 trial 的真实 Unix 时间戳重新采集 `action_trials.csv` 和 `expression_trials.csv`。

NPU 确认方式：

- 启动日志应出现 `pose_runtime`、`face_runtime`、`expression_runtime`，且 `backend` 均为 `acl_om_npu`。
- 逐帧日志应包含 `pose_backend=acl_om_npu`、`face_backend=acl_om_npu`、`expression_backend=acl_om_npu`。
- `npu-smi info` 的 AICore 是瞬时值，短间隔采样可能看到 0；建议联调时用 `watch -n 0.2 npu-smi info` 观察，同时以 JSONL runtime 字段作为是否走 NPU 的主判据。

## 关键配置

`run_pipeline.py` 当前默认配置：

```python
USE_UDP_STREAM = True
UDP_LISTEN_PORT = 9090
UDP_MODULE_PATH = "/root/userdata/ciciec/communication/build"

BACKEND_HOST = "192.168.137.1"
BACKEND_PORT = 9000
```

- 使用 PYNQ 硬件视频流时，保持 `USE_UDP_STREAM = True`，并确认 `UDP_MODULE_PATH` 或 `PYTHONPATH` 指向 `code/communication/build`。
- 使用本地摄像头时，将 `USE_UDP_STREAM = False`，`VIDEO_SOURCE = 0`。
- 使用视频文件时，将 `VIDEO_SOURCE` 改成视频路径。
- 如果 PC 后端不在 `192.168.137.1`，同步修改 `BACKEND_HOST`。

`scripts/video_player.py` 当前监听：

```python
WS_URL = "ws://127.0.0.1:8000/ws"
PC_VIDEO_CTRL_PORT = 5567
```

播放器优先使用 WebSocket 指令，同时兼容 UDP 控制通道。

## 模型文件

姿态识别：

- 310B 生产首选：`models/om/yolo26n-pose-640.om`
- 可选 NPU 路径：`models/onnx/yolo26n-pose-640.onnx` + OpenCV DNN CANN/NPU
- 本地调试权重：`yolo26n-pose.pt`

`PoseInfer` 默认强制 `ACL + OM + NPU`，启动日志会打印 `pose_backend=acl_om_npu` 以及 OM 输入/输出尺寸；如果 CANN/ACL 未加载或 `.om` 不可用，会直接报错，避免静默回退到 CPU/PT。需要临时指定模型可设置：

```bash
export SMART_IP_POSE_MODEL=models/om/yolo26n-pose-640.om
```

仅本地调试时才建议显式打开 PT 回退：

```bash
export SMART_IP_POSE_REQUIRE_NPU=0
export SMART_IP_POSE_ALLOW_PT_FALLBACK=1
export SMART_IP_POSE_MODEL=/home/bb/competition_ws/ciciec_ascend/code/ascend_310B/smart_ip_interaction/yolo26n-pose.pt
```

表情识别：

- 人脸检测生产首选：`models/om/face_detection_yunet_2023mar_640.om`
- 表情分类生产首选：`models/om/emotion_mobilenetv3_6cls.om`
- 本地调试人脸 ONNX：`models/pretrained/face_detection_yunet_2023mar.onnx`
- 本地调试表情 ONNX：`models/onnx/emotion_mobilenetv3_6cls.onnx`
- 本地调试训练权重：`models/checkpoints/emotion_mobilenetv3_6cls_best.pth`
- 类别顺序：`angry`、`fear`、`happy`、`neutral`、`sad`、`surprised`

人脸检测和表情分类默认都强制 `ACL + OM + NPU`，启动日志会分别写出 `face_backend=acl_om_npu`、`expression_backend=acl_om_npu`；如果 `.om` 或 CANN/ACL 环境不可用，会直接报错，避免回退到 CPU 路径。需要临时指定模型可设置：

```bash
export SMART_IP_FACE_MODEL=/root/userdata/ciciec/ascend_310B/smart_ip_interaction/models/om/face_detection_yunet_2023mar_640.om
export SMART_IP_EMOTION_MODEL=/root/userdata/ciciec/ascend_310B/smart_ip_interaction/models/om/emotion_mobilenetv3_6cls.om
```

仅本地调试时才建议显式打开 OpenCV/PTH 回退：

```bash
export SMART_IP_FACE_REQUIRE_NPU=0
export SMART_IP_FACE_ALLOW_OPENCV_FALLBACK=1
export SMART_IP_EXPR_REQUIRE_NPU=0
export SMART_IP_EXPR_ALLOW_TORCH_FALLBACK=1
```

表情分类使用 7 帧概率均值做平滑。

## 动作与事件

动作规则基于 COCO 17 点关键点：

| 动作 | 触发条件 |
|---|---|
| WAVE | 任一手腕高于肩膀一定比例 |
| HEART | 双腕靠近并位于胸前区域 |
| CHEER | 双手同时高于头部/鼻子附近 |

动作状态机默认连续 2 帧确认，冷却 0.8 秒，优先级为 `CHEER > HEART > WAVE`。

推理进程发送到后端的事件示例：

```json
{"type": "action", "value": "wave_hand", "keypoints": [[x, y]]}
{"type": "action", "value": "heart_particles", "keypoints": [[x, y]]}
{"type": "action", "value": "cheer_light_sound"}
{"type": "expression", "value": "smile_feedback", "face_bbox": [x1, y1, x2, y2]}
```

后端 `backend/server.py` 内置逻辑：

- `wave_hand` 触发 `wave_response`。
- `heart_particles` 触发 `heart_response`，附带关键点时额外广播 `ar_overlay`。
- `cheer_light_sound` 累计 3 次触发一次 `switch_scene`。
- 表情事件映射到 `smile_response`、`wow_response`、`angry_response`、`fear_response`、`sad_response`。
- 后端对同一事件有 2 秒冷却。

## 视频素材

视频映射由 `configs/video_config.json` 控制，路径相对本目录：

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

当前仓库已包含 `idle/idle.mp4`、`wave/wave.mp4`、`wave/wave2.mp4`、`heart/heart.mp4`。`cheer` 和表情回应目录可按配置补齐素材；缺失时播放器会打印告警并忽略对应指令。

## 训练入口

表情训练和导出：

```bash
python training/emotion/train_emotion_6cls.py
python training/emotion/export_emotion_onnx.py
```

动作规则调试：

```bash
python training/pose/test_action_rules.py
```

## 注意事项

- `run_pipeline.py` 无可用 GUI 时会禁用 `cv2.imshow`，推理仍继续运行。
- UDP 视频流依赖 `code/communication` 的 pybind11 模块；导入失败时会尝试回退到本地摄像头。
- 姿态、人脸检测、表情分类默认不再回退到 CPU/PT/PTH；现场确认 `pipeline.jsonl` 中的 `pose_runtime`、`face_runtime`、`expression_runtime` 都应显示 `acl_om_npu`。
- 每次正式压测建议使用新的日志目录或清空旧 JSONL；如果多个 `pipeline_start` 追加到同一目录，直接汇总会污染持续时间和 FPS。
- 本目录只负责视觉互动链路；角色语音对话模块在 `../character_voice_chat/`，当前尚未完整接入该链路。
