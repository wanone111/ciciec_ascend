"""
超轻量人脸检测器（YuNet）
============================
两段式级联第一段：全帧检测所有人脸，返回置信度最高的那个 bbox。
模型体积 ~400KB，纯 OpenCV DNN 推理，毫秒级延迟。
"""

import cv2
import numpy as np


class FaceDetector:
    def __init__(self, model_path, score_threshold=0.6, nms_threshold=0.3):
        self.detector = cv2.FaceDetectorYN.create(
            model=model_path,
            config="",
            input_size=(320, 320),
            score_threshold=score_threshold,
            nms_threshold=nms_threshold,
            top_k=5,
        )
        self._last_input_size = (320, 320)

    def detect_best(self, frame):
        """
        检测帧内所有人脸，返回置信度最高的那个。
        返回值：(roi_bgr, (x1,y1,x2,y2)) 或 (None, None)
        """
        h, w = frame.shape[:2]

        # YuNet 要求输入尺寸与 setInputSize 一致
        if self._last_input_size != (w, h):
            self.detector.setInputSize((w, h))
            self._last_input_size = (w, h)

        _, faces = self.detector.detect(frame)

        if faces is None or len(faces) == 0:
            return None, None

        # 取置信度最高的人脸（faces 已按置信度降序排列）
        best = faces[0]
        x, y, fw, fh = int(best[0]), int(best[1]), int(best[2]), int(best[3])

        # 适当扩边，避免裁剪过紧
        pad = int(max(fw, fh) * 0.15)
        x1 = max(0, x - pad)
        y1 = max(0, y - pad)
        x2 = min(w, x + fw + pad)
        y2 = min(h, y + fh + pad)

        roi = frame[y1:y2, x1:x2]
        if roi.size == 0:
            return None, None

        return roi, (x1, y1, x2, y2)

    def detect_all(self, frame):
        """返回所有人脸的 bbox 列表 [(x1,y1,x2,y2), ...]。"""
        h, w = frame.shape[:2]
        if self._last_input_size != (w, h):
            self.detector.setInputSize((w, h))
            self._last_input_size = (w, h)

        _, faces = self.detector.detect(frame)
        if faces is None:
            return []

        result = []
        for f in faces:
            x, y, fw, fh = int(f[0]), int(f[1]), int(f[2]), int(f[3])
            result.append((x, y, x + fw, y + fh))
        return result
