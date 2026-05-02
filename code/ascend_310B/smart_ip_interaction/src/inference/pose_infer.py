"""
Pose inference wrapper.

Prefers ONNX + OpenCV DNN with CANN/NPU backend when available.
Falls back to Ultralytics YOLO on PT models when no ONNX/NPU path is available.
"""

from __future__ import annotations

import os
from dataclasses import dataclass

import cv2
import numpy as np
import torch
from ultralytics import YOLO
from ultralytics.engine.results import Results
from ultralytics.utils import nms, ops


@dataclass
class PoseModelConfig:
    model_path: str | None = None
    imgsz: int = 640
    conf: float = 0.25
    iou: float = 0.45
    use_npu: bool = True


class PoseInfer:
    """Pose inference with ONNX/CANN first, PT fallback."""

    def __init__(self, model_path: str | None = None, imgsz: int = 640,
                 conf: float = 0.25, iou: float = 0.45, use_npu: bool = True):
        self.cfg = PoseModelConfig(model_path=model_path, imgsz=imgsz, conf=conf, iou=iou, use_npu=use_npu)
        self.names = {0: "person"}
        self.kpt_shape = (17, 3)

        self._torch_model = None
        self._net = None
        self._ratio_pad = None
        self._input_shape = (self.cfg.imgsz, self.cfg.imgsz)
        self._project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

        resolved = self._resolve_model_path(self.cfg.model_path)
        self.model_path = resolved

        if resolved.lower().endswith(".onnx"):
            try:
                self._net = cv2.dnn.readNetFromONNX(resolved)
                self._configure_backend()
                print(f"[POSE] using ONNX model: {resolved}")
            except cv2.error as e:
                print(f"[POSE] failed to load ONNX model, fallback to PT: {e}")
                self._net = None
                fallback_pt = self._resolve_fallback_pt(self._project_root)
                self._torch_model = YOLO(fallback_pt)
                self.model_path = fallback_pt
        else:
            self._torch_model = YOLO(resolved)
            print(f"[POSE] using Ultralytics PT model: {resolved}")

    def _resolve_model_path(self, preferred: str | None) -> str:
        candidates = []
        if preferred:
            candidates.append(preferred)

        project_root = self._project_root
        candidates.extend([
            os.path.join(project_root, "models", "onnx", "yolo26s-pose.onnx"),
            os.path.join(project_root, "..", "..", "..", "..", "test", "yolo26s-pose.onnx"),
            os.path.join(project_root, "yolo26n-pose.pt"),
        ])

        for path in candidates:
            if path and os.path.exists(path):
                return os.path.abspath(path)

        raise FileNotFoundError(
            "Cannot find pose model. Expected an ONNX/PT file in models/onnx, project root, "
            "or ../../../../test/yolo26s-pose.onnx."
        )

    @staticmethod
    def _resolve_fallback_pt(project_root: str) -> str:
        fallback = os.path.join(project_root, "yolo26n-pose.pt")
        if os.path.exists(fallback):
            return fallback
        raise FileNotFoundError(f"Cannot find fallback PT pose model: {fallback}")

    def _configure_backend(self) -> None:
        if self._net is None:
            return

        if self.cfg.use_npu and hasattr(cv2.dnn, "DNN_BACKEND_CANN") and hasattr(cv2.dnn, "DNN_TARGET_NPU"):
            try:
                self._net.setPreferableBackend(cv2.dnn.DNN_BACKEND_CANN)
                self._net.setPreferableTarget(cv2.dnn.DNN_TARGET_NPU)
                print("[POSE] CANN/NPU backend enabled")
                return
            except cv2.error:
                pass

        self._net.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
        self._net.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)
        print("[POSE] fallback to OpenCV CPU backend")

    @staticmethod
    def _letterbox(image: np.ndarray, new_shape: int = 640, color: tuple[int, int, int] = (114, 114, 114)):
        shape = image.shape[:2]  # h, w
        if isinstance(new_shape, int):
            new_shape = (new_shape, new_shape)

        r = min(new_shape[0] / shape[0], new_shape[1] / shape[1])
        new_unpad = (int(round(shape[1] * r)), int(round(shape[0] * r)))
        dw, dh = new_shape[1] - new_unpad[0], new_shape[0] - new_unpad[1]
        dw /= 2
        dh /= 2

        if shape[::-1] != new_unpad:
            image = cv2.resize(image, new_unpad, interpolation=cv2.INTER_LINEAR)

        top, bottom = int(round(dh - 0.1)), int(round(dh + 0.1))
        left, right = int(round(dw - 0.1)), int(round(dw + 0.1))
        image = cv2.copyMakeBorder(image, top, bottom, left, right, cv2.BORDER_CONSTANT, value=color)
        return image, ((r, r), (dw, dh))

    def _forward_onnx(self, frame: np.ndarray):
        img, ratio_pad = self._letterbox(frame, self.cfg.imgsz)
        blob = cv2.dnn.blobFromImage(img, scalefactor=1 / 255.0, size=self._input_shape, swapRB=True, crop=False)
        self._net.setInput(blob)
        outputs = self._net.forward()
        if isinstance(outputs, (tuple, list)):
            outputs = outputs[0]
        preds = np.asarray(outputs)
        if preds.ndim == 2:
            preds = preds[None, ...]
        if preds.ndim != 3:
            raise RuntimeError(f"Unexpected pose output shape: {preds.shape}")

        # Ultralytics ONNX export usually returns either (1, channels, anchors) or (1, anchors, channels)
        if preds.shape[1] > preds.shape[2]:
            preds = np.transpose(preds, (0, 2, 1))

        preds_t = torch.from_numpy(preds)
        dets = nms.non_max_suppression(preds_t, self.cfg.conf, self.cfg.iou, nc=1)
        det = dets[0]

        if det is None or len(det) == 0:
            return [Results(frame, path="", names=self.names)]

        det = det.clone()
        det[:, :4] = ops.scale_boxes(self._input_shape, det[:, :4], frame.shape, ratio_pad=ratio_pad)

        kpts = det[:, 6:].clone().view(-1, self.kpt_shape[0], self.kpt_shape[1])
        kpts_xy = kpts[..., :2].reshape(-1, 2)
        ops.scale_coords(self._input_shape, kpts_xy, frame.shape, ratio_pad=ratio_pad)
        kpts[..., :2] = kpts_xy.view(-1, self.kpt_shape[0], 2)

        result = Results(frame, path="", names=self.names, boxes=det[:, :6], keypoints=kpts)
        return [result]

    def __call__(self, frame: np.ndarray, verbose: bool = False):
        if self._net is not None:
            return self._forward_onnx(frame)

        return self._torch_model(frame, verbose=verbose)
