# 算法接口文档 v1.0

> 项目：智能视觉交互系统（2026集创赛）
> 芯片：华为昇腾 310B / 310P
> 版本：v1.0 | 2026-04-13

---

## 1. 整体数据流

```
摄像头/视频帧
    │
    ▼
┌─────────────────────────┐
│  Stage A：姿态检测       │  yolo26n-pose.pt → .om
│  输入：BGR图像           │
│  输出：17个关键点坐标    │
└────────────┬────────────┘
             │ 关键点
             ▼
┌─────────────────────────┐
│  动作状态机              │
│  输出：动作标签          │
└─────────────────────────┘

┌─────────────────────────┐
│  Stage B1：人脸检测      │  YuNet ONNX（~400KB）
│  输入：BGR图像           │
│  输出：人脸 bbox         │
└────────────┬────────────┘
             │ ROI裁剪
             ▼
┌─────────────────────────┐
│  Stage B2：表情分类      │  emotion_mobilenetv3_6cls.onnx
│  输入：人脸ROI           │
│  输出：表情标签+置信度   │
└─────────────────────────┘
             │
             ▼
┌─────────────────────────┐
│  TCP JSON 推送           │  → 后端 9000 端口
└─────────────────────────┘
```

---

## 2. 姿态检测模型接口

### 2.1 模型信息

| 项目 | 参数 |
|------|------|
| 模型文件 | `yolo26n-pose.pt` → 转换后 `yolo26n-pose.om` |
| 输入尺寸 | 640 × 640 × 3 (BGR) |
| 输入类型 | float32，归一化到 [0, 1] |
| 关键点格式 | COCO 17点 |
| 输出 | bbox (x1,y1,x2,y2,conf) + 17×2 关键点像素坐标 |

### 2.2 COCO 17 关键点索引

| 索引 | 名称 | 索引 | 名称 |
|------|------|------|------|
| 0 | NOSE | 9 | LEFT_WRIST |
| 1 | LEFT_EYE | 10 | RIGHT_WRIST |
| 2 | RIGHT_EYE | 11 | LEFT_HIP |
| 3 | LEFT_EAR | 12 | RIGHT_HIP |
| 4 | RIGHT_EAR | 13 | LEFT_KNEE |
| 5 | LEFT_SHOULDER | 14 | RIGHT_KNEE |
| 6 | RIGHT_SHOULDER | 15 | LEFT_ANKLE |
| 7 | LEFT_ELBOW | 16 | RIGHT_ANKLE |
| 8 | RIGHT_ELBOW | | |

### 2.3 动作判断逻辑（基于规则，无需额外模型）

| 动作 | 判断条件 | 触发阈值 |
|------|----------|----------|
| WAVE（挥手） | 任一手腕 y < 肩膀均值 y − 0.1×肩宽 | 连续2帧 |
| CHEER（欢呼） | 双手腕 y < 鼻子 y − 0.15×肩宽 | 连续2帧 |
| HEART（比心） | 双腕距 < 0.9×肩宽 且 双腕中点在胸前区域 | 连续2帧 |

优先级：CHEER > HEART > WAVE，触发后冷却 0.8s

---

## 3. 表情分类模型接口

### 3.1 模型信息

| 项目 | 参数 |
|------|------|
| 模型文件 | `emotion_mobilenetv3_6cls.onnx` → 转换后 `.om` |
| 骨干网络 | MobileNetV3-Small |
| 参数量 | ~1.5M |
| 输入尺寸 | 112 × 112 × 3 (RGB) |
| 输入类型 | float32，归一化到 [0, 1] |
| 输出 | logits，shape (1, 6)，需 softmax |

### 3.2 输出类别（按 ImageFolder 字母序）

| 索引 | 类别 | 中文含义 | 推荐置信度阈值 |
|------|------|----------|--------------|
| 0 | angry | 生气 | 0.60 |
| 1 | fear | 恐惧 | 0.60 |
| 2 | happy | 开心 | 0.65 |
| 3 | neutral | 平静 | 0.50 |
| 4 | sad | 悲伤 | 0.60 |
| 5 | surprised | 惊讶 | 0.65 |

### 3.3 前处理流程

```
BGR图像 (任意尺寸)
    → 转 RGB
    → Resize(112, 112)
    → ToTensor()  [0,255] → [0.0, 1.0]
    → 扩维 unsqueeze(0)  → shape (1, 3, 112, 112)
```

### 3.4 后处理

```
logits (1,6) → softmax → probs (1,6)
滑动窗口7帧均值投票 → 取 argmax → 类别标签 + 置信度
```

---

## 4. 人脸检测模型接口（两段式第一段）

| 项目 | 参数 |
|------|------|
| 模型文件 | `face_detection_yunet_2023mar.onnx` |
| 框架 | OpenCV FaceDetectorYN |
| 输入尺寸 | 与输入帧相同（动态） |
| 输出 | [x, y, w, h, 置信度, ...] 每行一个人脸 |
| 置信度阈值 | 0.6 |
| NMS阈值 | 0.3 |

---

## 5. TCP 通信数据包格式

边缘端（310B）→ PC后端（port 9000），每条消息以 `\n` 结尾的 JSON：

### 5.1 动作事件

```json
{
  "type": "action",
  "value": "wave_hand",
  "keypoints": [[x0,y0], [x1,y1], "...17个点"],
  "timestamp": 1744550400.123
}
```

`value` 可选值：`wave_hand` | `heart_particles` | `cheer_light_sound`

### 5.2 表情事件

```json
{
  "type": "expression",
  "value": "smile_feedback",
  "face_bbox": [x1, y1, x2, y2],
  "keypoints": [[x0,y0], "..."],
  "timestamp": 1744550401.456
}
```

`value` 可选值：`smile_feedback` | `wow_feedback` | `angry_feedback` | `fear_feedback` | `sad_feedback` | `idle_feedback`

---

## 6. 后端广播格式（WebSocket port 8000）

后端处理后广播给前端（Unity/视频展示程序）：

### 6.1 对话触发

```json
{
  "scene": "dialogue",
  "character": "bear",
  "dialogue_id": "wave_response",
  "tts": "你好呀！欢迎来到熊乐园！",
  "face_bbox": [x1, y1, x2, y2]
}
```

### 6.2 欢呼特效

```json
{
  "scene": "cheer_effect",
  "cheer_total": 3
}
```

### 6.3 剧情切换

```json
{
  "scene": "switch_scene",
  "scene_id": 1,
  "trigger": "cheer",
  "cheer_total": 3
}
```

### 6.4 AR 合影

```json
{
  "scene": "ar_overlay",
  "effect": "bear_hat",
  "face_bbox": [x1, y1, x2, y2],
  "keypoints": [[x0,y0], "..."]
}
```

---

## 7. 性能指标（PC端参考值）

| 模块 | 耗时（CPU） | 耗时（GPU/NPU 预估） |
|------|------------|-------------------|
| yolo26n-pose 推理 | ~30ms | <5ms |
| YuNet 人脸检测 | ~5ms | <2ms |
| MobileNetV3 表情分类 | ~10ms | <3ms |
| 端到端流水线 FPS | ~15fps | >30fps |

---

*文档维护：软件组 | 如有变更请同步通知嵌入式组*
