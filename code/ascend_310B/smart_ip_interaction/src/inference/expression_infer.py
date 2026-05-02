from collections import deque
import os

import cv2
import numpy as np
import torch
import torch.nn as nn
from PIL import Image
from torchvision import models, transforms

# ImageFolder 按字母序加载：angry=0, fear=1, happy=2, neutral=3, sad=4, surprised=5
CLASS_NAMES_6 = ["angry", "fear", "happy", "neutral", "sad", "surprised"]
CLASS_NAMES_3 = ["happy", "neutral", "surprised"]

# COCO keypoint indices for face
_NOSE = 0
_LEFT_EYE = 1
_RIGHT_EYE = 2
_LEFT_EAR = 3
_RIGHT_EAR = 4


class ExpressionInfer:
    def __init__(self, model_path, device=None, img_size=112, num_classes=6,
                 smooth_window=7, use_npu=True):
        self.device = device or ("cuda" if torch.cuda.is_available() else "cpu")
        self.img_size = img_size
        self.num_classes = num_classes
        self.class_names = CLASS_NAMES_6 if num_classes == 6 else CLASS_NAMES_3
        self.use_npu = use_npu

        self._net = None
        self._torch_backend = False

        if model_path.lower().endswith(".onnx") and os.path.exists(model_path):
            self._net = cv2.dnn.readNetFromONNX(model_path)
            backend_ready = False
            if self.use_npu and hasattr(cv2.dnn, "DNN_BACKEND_CANN") and hasattr(cv2.dnn, "DNN_TARGET_NPU"):
                try:
                    self._net.setPreferableBackend(cv2.dnn.DNN_BACKEND_CANN)
                    self._net.setPreferableTarget(cv2.dnn.DNN_TARGET_NPU)
                    print(f"[EXPR] using ONNX + CANN/NPU: {model_path}")
                    backend_ready = True
                except cv2.error:
                    backend_ready = False

            if not backend_ready:
                self._net.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
                self._net.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)
                print(f"[EXPR] using ONNX + CPU: {model_path}")
        else:
            self._torch_backend = True

        if self._torch_backend:
            self.model = models.mobilenet_v3_small(weights=None)
            self.model.classifier[3] = nn.Linear(
                self.model.classifier[3].in_features, num_classes
            )
            self.model.load_state_dict(
                torch.load(model_path, map_location=self.device)
            )
            self.model.to(self.device)
            self.model.eval()
        else:
            self.model = None

        self.transform = transforms.Compose([
            transforms.Resize((self.img_size, self.img_size)),
            transforms.ToTensor(),
        ])

        # 滑动窗口投票，防止单帧抖动
        self._smooth_window = smooth_window
        self._vote_buffer = deque(maxlen=smooth_window)

    # ------------------------------------------------------------------
    # 推理
    # ------------------------------------------------------------------
    def predict_bgr(self, bgr_img):
        """输入 BGR numpy 图像，返回原始单帧结果（不含平滑）。"""
        if self._net is not None:
            rgb = cv2.cvtColor(bgr_img, cv2.COLOR_BGR2RGB)
            blob = cv2.dnn.blobFromImage(
                rgb,
                scalefactor=1.0 / 255.0,
                size=(self.img_size, self.img_size),
                swapRB=False,
                crop=False,
            )
            self._net.setInput(blob)
            logits = np.asarray(self._net.forward()).reshape(-1)
            logits = logits.astype(np.float32)
            exp = np.exp(logits - np.max(logits))
            probs = exp / np.sum(exp)
            pred_idx = int(np.argmax(probs))
        else:
            rgb = cv2.cvtColor(bgr_img, cv2.COLOR_BGR2RGB)
            pil_img = Image.fromarray(rgb)
            x = self.transform(pil_img).unsqueeze(0).to(self.device)

            with torch.no_grad():
                logits = self.model(x)
                probs = torch.softmax(logits, dim=1)[0].cpu().numpy()
                pred_idx = int(probs.argmax())

        return {
            "expression": self.class_names[pred_idx],
            "confidence": float(probs[pred_idx]),
            "probs": np.asarray(probs)
        }

    def predict_smooth(self, bgr_img):
        """推理 + 滑动窗口多数投票，返回平滑后的表情标签和置信度。"""
        raw = self.predict_bgr(bgr_img)
        self._vote_buffer.append(raw["probs"])

        # 对窗口内所有帧的 softmax 概率求均值
        avg_probs = np.mean(self._vote_buffer, axis=0)
        smooth_idx = int(avg_probs.argmax())
        return {
            "expression": self.class_names[smooth_idx],
            "confidence": float(avg_probs[smooth_idx])
        }

    def reset_smooth(self):
        self._vote_buffer.clear()

    # ------------------------------------------------------------------
    # 人脸 ROI 提取
    # ------------------------------------------------------------------
    def face_roi_from_keypoints(self, frame, kp, padding=0.6):
        """
        利用姿态关键点（COCO 格式）推算人脸 bbox 并裁剪。
        kp: shape (17, 2) 或 (17, 3) tensor/array，坐标为像素值。
        返回 (roi_bgr, (x1,y1,x2,y2))；失败返回 (None, None)。
        """
        h, w = frame.shape[:2]
        kp = np.array(kp, dtype=float)

        # 取鼻子、双眼、双耳中有效点（坐标非零）
        face_indices = [_NOSE, _LEFT_EYE, _RIGHT_EYE, _LEFT_EAR, _RIGHT_EAR]
        pts = []
        for idx in face_indices:
            x, y = kp[idx][0], kp[idx][1]
            if x > 1 and y > 1:  # 过滤掉未检测到的点（通常为0）
                pts.append((x, y))

        if len(pts) < 2:
            return None, None

        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        cx = (min(xs) + max(xs)) / 2
        cy = (min(ys) + max(ys)) / 2
        # 用人脸宽度估算包围盒尺寸
        face_w = max(max(xs) - min(xs), 20)
        half = face_w * (0.5 + padding)

        x1 = max(0, int(cx - half))
        y1 = max(0, int(cy - half))
        x2 = min(w, int(cx + half))
        y2 = min(h, int(cy + half))

        if x2 - x1 < 10 or y2 - y1 < 10:
            return None, None

        roi = frame[y1:y2, x1:x2]
        return roi, (x1, y1, x2, y2)

    def center_crop_face(self, frame, crop_ratio=0.35):
        """备用：无关键点时用中心裁剪。"""
        h, w = frame.shape[:2]
        cw = int(w * crop_ratio)
        ch = int(h * crop_ratio)
        cx, cy = w // 2, h // 2
        x1 = max(0, cx - cw // 2)
        y1 = max(0, cy - ch // 2)
        x2 = min(w, cx + cw // 2)
        y2 = min(h, cy + ch // 2)
        return frame[y1:y2, x1:x2], (x1, y1, x2, y2)
