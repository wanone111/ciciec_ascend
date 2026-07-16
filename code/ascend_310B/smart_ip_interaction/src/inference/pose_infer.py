"""
Pose inference wrapper for Ascend 310B.

The production path is ACL + OM. ONNX+CANN is kept as an explicit secondary
NPU path, while PT/Ultralytics loading is disabled unless callers opt in.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from functools import reduce
from operator import mul

import cv2
import numpy as np

from inference.acl_om import AclOmSession as _CommonAclOmSession


_SKELETON = (
    (16, 14), (14, 12), (17, 15), (15, 13), (12, 13), (6, 12), (7, 13),
    (6, 7), (6, 8), (7, 9), (8, 10), (9, 11), (2, 3), (1, 2), (1, 3),
    (2, 4), (3, 5), (4, 6), (5, 7),
)


@dataclass
class PoseModelConfig:
    model_path: str | None = None
    imgsz: int = 640
    conf: float = 0.25
    iou: float = 0.45
    use_npu: bool = True
    require_npu: bool = True
    allow_pt_fallback: bool = False
    device_id: int = 0


class PoseBoxes:
    def __init__(self, xyxy: np.ndarray, conf: np.ndarray | None = None):
        self.xyxy = np.asarray(xyxy, dtype=np.float32).reshape(-1, 4)
        self.conf = (
            np.asarray(conf, dtype=np.float32).reshape(-1)
            if conf is not None else np.zeros((len(self.xyxy),), dtype=np.float32)
        )

    def __len__(self) -> int:
        return len(self.xyxy)


class PoseKeypoints:
    def __init__(self, data: np.ndarray):
        data = np.asarray(data, dtype=np.float32).reshape(-1, 17, 3)
        self.data = data
        self.xy = data[..., :2]
        self.conf = data[..., 2]

    def __len__(self) -> int:
        return len(self.data)


class PoseResult:
    """Small result object matching the subset used by run_pipeline.py."""

    def __init__(self, frame: np.ndarray, boxes: np.ndarray | None = None,
                 keypoints: np.ndarray | None = None, scores: np.ndarray | None = None):
        self.orig_img = frame
        self.names = {0: "person"}
        self.boxes = PoseBoxes(np.empty((0, 4), dtype=np.float32)) if boxes is None else PoseBoxes(boxes, scores)
        self.keypoints = (
            PoseKeypoints(np.empty((0, 17, 3), dtype=np.float32))
            if keypoints is None else PoseKeypoints(keypoints)
        )

    def plot(self) -> np.ndarray:
        image = self.orig_img.copy()
        for box, score, kpts in zip(self.boxes.xyxy, self.boxes.conf, self.keypoints.data):
            x1, y1, x2, y2 = [int(round(v)) for v in box]
            cv2.rectangle(image, (x1, y1), (x2, y2), (0, 220, 80), 2)
            cv2.putText(
                image,
                f"person {float(score):.2f}",
                (x1, max(y1 - 8, 12)),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.55,
                (0, 220, 80),
                2,
            )

            for i1, i2 in _SKELETON:
                p1 = kpts[i1 - 1]
                p2 = kpts[i2 - 1]
                if p1[2] > 0.5 and p2[2] > 0.5:
                    cv2.line(
                        image,
                        (int(p1[0]), int(p1[1])),
                        (int(p2[0]), int(p2[1])),
                        (0, 255, 0),
                        2,
                    )
            for x, y, conf in kpts:
                if conf > 0.5:
                    cv2.circle(image, (int(x), int(y)), 4, (0, 255, 255), -1)
        return image


_AclOmSession = _CommonAclOmSession


class PoseInfer:
    """Pose inference with strict NPU-first runtime selection."""

    def __init__(
        self,
        model_path: str | None = None,
        imgsz: int = 640,
        conf: float = 0.25,
        iou: float = 0.45,
        use_npu: bool = True,
        require_npu: bool = True,
        allow_pt_fallback: bool = False,
        device_id: int = 0,
    ):
        self.cfg = PoseModelConfig(
            model_path=model_path,
            imgsz=imgsz,
            conf=conf,
            iou=iou,
            use_npu=use_npu,
            require_npu=require_npu,
            allow_pt_fallback=allow_pt_fallback,
            device_id=device_id,
        )
        self.names = {0: "person"}
        self.kpt_shape = (17, 3)

        self._torch_model = None
        self._net = None
        self._om_session: _AclOmSession | None = None
        self._backend = "uninitialized"
        self._input_shape = (self.cfg.imgsz, self.cfg.imgsz)
        self.reuse_input_buffer = _env_bool("SMART_IP_REUSE_INPUT_BUFFER", True)
        self._om_input_buffer: np.ndarray | None = None
        self._project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

        resolved = self._resolve_model_path(self.cfg.model_path)
        self.model_path = resolved

        suffix = os.path.splitext(resolved)[1].lower()
        if suffix == ".om":
            self._load_om(resolved)
        elif suffix == ".onnx":
            self._load_onnx(resolved)
        elif suffix == ".pt":
            self._load_pt(resolved)
        else:
            raise RuntimeError(f"Unsupported pose model format: {resolved}")

    @property
    def backend(self) -> str:
        return self._backend

    def runtime_info(self) -> dict[str, Any]:
        info: dict[str, Any] = {
            "pose_backend": self._backend,
            "pose_model_path": self.model_path,
            "pose_require_npu": self.cfg.require_npu,
            "pose_allow_pt_fallback": self.cfg.allow_pt_fallback,
            "pose_device_id": self.cfg.device_id,
        }
        if self._om_session is not None:
            info.update(
                pose_om_input_sizes=self._om_session.input_sizes,
                pose_om_output_sizes=self._om_session.output_sizes,
                pose_om_input_dims=self._om_session.input_dims,
                pose_om_output_dims=self._om_session.output_dims,
            )
        return info

    def _resolve_model_path(self, preferred: str | None) -> str:
        candidates: list[str] = []
        if preferred:
            candidates.append(preferred)

        project_root = self._project_root
        candidates.append(os.path.join(project_root, "models", "om", "yolo26s-pose.om"))

        if not self.cfg.require_npu or self.cfg.allow_pt_fallback:
            candidates.extend([
                os.path.join(project_root, "models", "onnx", "yolo26s-pose.onnx"),
                os.path.join(project_root, "..", "..", "..", "..", "test", "yolo26s-pose.onnx"),
            ])
        if self.cfg.allow_pt_fallback:
            candidates.append(os.path.join(project_root, "yolo26n-pose.pt"))

        seen = set()
        for path in candidates:
            if not path:
                continue
            abs_path = os.path.abspath(path)
            if abs_path in seen:
                continue
            seen.add(abs_path)
            if os.path.exists(abs_path):
                return abs_path

        raise FileNotFoundError(
            "Cannot find pose model. Production expects models/om/yolo26s-pose.om. "
            "Set SMART_IP_POSE_MODEL to a valid .om path, or explicitly allow a "
            "non-production fallback in code."
        )

    def _load_om(self, model_path: str) -> None:
        if not self.cfg.use_npu:
            raise RuntimeError("Refusing to load OM with use_npu=False")
        self._om_session = _AclOmSession(model_path, self.cfg.device_id)
        self._backend = "acl_om_npu"
        print(f"[POSE] using ACL OM/NPU model: {model_path}")
        print(f"[POSE] OM input sizes={self._om_session.input_sizes}, output sizes={self._om_session.output_sizes}")

    def _load_onnx(self, model_path: str) -> None:
        try:
            self._net = cv2.dnn.readNetFromONNX(model_path)
        except cv2.error as exc:
            raise RuntimeError(f"Failed to load pose ONNX model: {model_path}") from exc

        if self.cfg.use_npu and hasattr(cv2.dnn, "DNN_BACKEND_CANN") and hasattr(cv2.dnn, "DNN_TARGET_NPU"):
            try:
                self._net.setPreferableBackend(cv2.dnn.DNN_BACKEND_CANN)
                self._net.setPreferableTarget(cv2.dnn.DNN_TARGET_NPU)
                self._backend = "opencv_dnn_cann_npu"
                print(f"[POSE] using ONNX + OpenCV CANN/NPU: {model_path}")
                return
            except cv2.error as exc:
                if self.cfg.require_npu:
                    raise RuntimeError("OpenCV exposes CANN/NPU but failed to enable it for pose ONNX") from exc

        if self.cfg.require_npu:
            raise RuntimeError(
                "Pose ONNX was selected, but OpenCV DNN CANN/NPU is unavailable. "
                "Use models/om/yolo26s-pose.om with ACL, or rebuild OpenCV with CANN."
            )

        self._net.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
        self._net.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)
        self._backend = "opencv_dnn_cpu"
        print(f"[POSE] using ONNX + OpenCV CPU: {model_path}")

    def _load_pt(self, model_path: str) -> None:
        if not self.cfg.allow_pt_fallback:
            raise RuntimeError(
                "PT pose inference is disabled for the 310B production path. "
                "Use the OM model or set allow_pt_fallback=True only for local debugging."
            )
        from ultralytics import YOLO

        self._torch_model = YOLO(model_path)
        self._backend = "ultralytics_pt"
        print(f"[POSE] using Ultralytics PT model for local debug: {model_path}")

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

    def _prepare_om_input(self, image: np.ndarray) -> np.ndarray:
        if self._om_session is None:
            raise RuntimeError("OM session is not initialized")

        expected_size = self._om_session.input_sizes[0]
        dims = self._om_session.input_dims[0]
        layout = _infer_layout(dims)
        elem_count = _prod(dims) if dims else image.size
        dtype: type[np.float32] | type[np.uint8]
        if expected_size == elem_count * np.dtype(np.float32).itemsize:
            dtype = np.float32
        elif expected_size == elem_count:
            dtype = np.uint8
        else:
            raise RuntimeError(
                f"Unsupported pose OM input contract: dims={dims}, expected_size={expected_size}. "
                "Expected float32 or uint8 image tensor."
            )

        if dtype is np.float32:
            if layout == "NCHW" and self.reuse_input_buffer:
                shape = tuple(dims) if dims else (1, 3, image.shape[0], image.shape[1])
                if self._om_input_buffer is None or self._om_input_buffer.shape != shape:
                    self._om_input_buffer = np.empty(shape, dtype=np.float32)
                tensor = self._om_input_buffer
                np.multiply(image[:, :, 2], 1.0 / 255.0, out=tensor[0, 0], casting="unsafe")
                np.multiply(image[:, :, 1], 1.0 / 255.0, out=tensor[0, 1], casting="unsafe")
                np.multiply(image[:, :, 0], 1.0 / 255.0, out=tensor[0, 2], casting="unsafe")
                return tensor
            rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
            tensor = rgb.astype(np.float32) / 255.0
        else:
            rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
            tensor = rgb.astype(np.uint8)

        if layout == "NHWC":
            tensor = tensor[np.newaxis, ...]
        else:
            tensor = tensor.transpose(2, 0, 1)[np.newaxis, ...]
        return np.ascontiguousarray(tensor, dtype=dtype)

    def _forward_om(self, frame: np.ndarray):
        img, ratio_pad = self._letterbox(frame, self.cfg.imgsz)
        input_array = self._prepare_om_input(img)
        outputs = self._om_session.execute(input_array) if self._om_session is not None else []
        if not outputs:
            raise RuntimeError("Pose OM returned no outputs")
        output = _reshape_unknown_pose_output(outputs[0])
        return [self._postprocess_output(output, frame, ratio_pad)]

    def _forward_onnx(self, frame: np.ndarray):
        img, ratio_pad = self._letterbox(frame, self.cfg.imgsz)
        blob = cv2.dnn.blobFromImage(img, scalefactor=1 / 255.0, size=self._input_shape, swapRB=True, crop=False)
        self._net.setInput(blob)
        outputs = self._net.forward()
        if isinstance(outputs, (tuple, list)):
            outputs = outputs[0]
        return [self._postprocess_output(np.asarray(outputs), frame, ratio_pad)]

    def _postprocess_output(self, output: np.ndarray, frame: np.ndarray, ratio_pad) -> PoseResult:
        arr = np.asarray(output, dtype=np.float32)
        decoded = _as_decoded_detections(arr)
        if decoded is not None:
            return self._postprocess_decoded(decoded, frame, ratio_pad)
        return self._postprocess_raw(arr, frame, ratio_pad)

    def _postprocess_decoded(self, det: np.ndarray, frame: np.ndarray, ratio_pad) -> PoseResult:
        if det.size == 0:
            return PoseResult(frame)

        det = det.reshape(-1, det.shape[-1])
        det = det[det[:, 4] >= self.cfg.conf]
        if len(det) == 0:
            return PoseResult(frame)

        cols = det.shape[1]
        offset = 6 if cols >= 57 else 5
        boxes = _ensure_xyxy(det[:, :4].copy())
        scores = det[:, 4].copy()
        kpts = det[:, offset:offset + 51].copy().reshape(-1, 17, 3)

        keep = _nms_xyxy(boxes, scores, self.cfg.iou)
        boxes, scores, kpts = boxes[keep], scores[keep], kpts[keep]
        _scale_boxes(boxes, frame.shape, ratio_pad)
        _scale_keypoints(kpts, frame.shape, ratio_pad)
        return PoseResult(frame, boxes=boxes, keypoints=kpts, scores=scores)

    def _postprocess_raw(self, preds: np.ndarray, frame: np.ndarray, ratio_pad) -> PoseResult:
        raw = _raw_to_anchor_rows(preds)
        if raw.shape[1] < 56:
            raise RuntimeError(f"Unexpected raw pose output shape: {preds.shape}")

        scores = raw[:, 4].astype(np.float32)
        keep = scores >= self.cfg.conf
        if not np.any(keep):
            return PoseResult(frame)

        raw = raw[keep]
        scores = scores[keep]
        boxes = _xywh_to_xyxy(raw[:, :4].astype(np.float32))
        kpts = raw[:, 5:56].astype(np.float32).reshape(-1, 17, 3)

        keep_idx = _nms_xyxy(boxes, scores, self.cfg.iou)
        if keep_idx.size == 0:
            return PoseResult(frame)

        boxes, scores, kpts = boxes[keep_idx], scores[keep_idx], kpts[keep_idx]
        _scale_boxes(boxes, frame.shape, ratio_pad)
        _scale_keypoints(kpts, frame.shape, ratio_pad)
        return PoseResult(frame, boxes=boxes, keypoints=kpts, scores=scores)

    def __call__(self, frame: np.ndarray, verbose: bool = False):
        if self._om_session is not None:
            return self._forward_om(frame)
        if self._net is not None:
            return self._forward_onnx(frame)
        return self._torch_model(frame, verbose=verbose)

    def close(self) -> None:
        if self._om_session is not None:
            self._om_session.close()
            self._om_session = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


def _prod(values: list[int] | tuple[int, ...] | None) -> int:
    if not values:
        return 0
    return int(reduce(mul, values, 1))


def _env_bool(name: str, default: bool) -> bool:
    raw = os.environ.get(name)
    if raw is None:
        return default
    return raw.strip().lower() not in ("0", "false", "no", "off")


def _infer_layout(dims: list[int] | None) -> str:
    if dims and len(dims) == 4 and dims[-1] in (1, 3):
        return "NHWC"
    return "NCHW"


def _reshape_unknown_pose_output(output: np.ndarray) -> np.ndarray:
    arr = np.asarray(output, dtype=np.float32)
    if arr.ndim != 1:
        return arr
    if arr.size % 57 == 0 and arr.size // 57 <= 1000:
        return arr.reshape(-1, 57)
    if arr.size % 56 == 0:
        rows = arr.size // 56
        if rows <= 1000:
            return arr.reshape(rows, 56)
        return arr.reshape(1, 56, rows)
    return arr


def _as_decoded_detections(output: np.ndarray) -> np.ndarray | None:
    arr = output
    while arr.ndim > 2 and arr.shape[0] == 1:
        arr = arr[0]
    if arr.ndim == 2 and arr.shape[1] in (56, 57) and arr.shape[0] <= 1000:
        return arr
    return None


def _raw_to_anchor_rows(preds: np.ndarray) -> np.ndarray:
    arr = np.asarray(preds, dtype=np.float32)
    if arr.ndim == 2:
        if arr.shape[0] in (56, 57):
            return arr.T
        return arr
    if arr.ndim == 3:
        arr = arr[0]
        if arr.shape[0] in (56, 57):
            return arr.T
        return arr
    raise RuntimeError(f"Unexpected pose output shape: {preds.shape}")


def _xywh_to_xyxy(xywh: np.ndarray) -> np.ndarray:
    xyxy = xywh.copy()
    xyxy[:, 0] = xywh[:, 0] - xywh[:, 2] / 2
    xyxy[:, 1] = xywh[:, 1] - xywh[:, 3] / 2
    xyxy[:, 2] = xywh[:, 0] + xywh[:, 2] / 2
    xyxy[:, 3] = xywh[:, 1] + xywh[:, 3] / 2
    return xyxy


def _ensure_xyxy(boxes: np.ndarray) -> np.ndarray:
    if boxes.size == 0:
        return boxes.reshape(-1, 4)
    invalid = np.mean((boxes[:, 2] <= boxes[:, 0]) | (boxes[:, 3] <= boxes[:, 1]))
    if invalid > 0.5:
        return _xywh_to_xyxy(boxes)
    return boxes


def _nms_xyxy(boxes: np.ndarray, scores: np.ndarray, iou_thres: float, max_det: int = 100) -> np.ndarray:
    if len(boxes) == 0:
        return np.empty((0,), dtype=np.int64)

    x1, y1, x2, y2 = boxes.T
    areas = np.maximum(0.0, x2 - x1) * np.maximum(0.0, y2 - y1)
    order = scores.argsort()[::-1]
    keep: list[int] = []

    while order.size > 0 and len(keep) < max_det:
        i = int(order[0])
        keep.append(i)
        if order.size == 1:
            break

        rest = order[1:]
        xx1 = np.maximum(x1[i], x1[rest])
        yy1 = np.maximum(y1[i], y1[rest])
        xx2 = np.minimum(x2[i], x2[rest])
        yy2 = np.minimum(y2[i], y2[rest])
        inter = np.maximum(0.0, xx2 - xx1) * np.maximum(0.0, yy2 - yy1)
        union = areas[i] + areas[rest] - inter
        iou = inter / np.maximum(union, 1e-7)
        order = rest[iou <= iou_thres]

    return np.asarray(keep, dtype=np.int64)


def _scale_boxes(boxes: np.ndarray, frame_shape, ratio_pad) -> None:
    gain = float(ratio_pad[0][0])
    pad_x, pad_y = ratio_pad[1]
    boxes[:, [0, 2]] -= float(pad_x)
    boxes[:, [1, 3]] -= float(pad_y)
    boxes[:, :4] /= gain
    h, w = frame_shape[:2]
    boxes[:, [0, 2]] = boxes[:, [0, 2]].clip(0, w)
    boxes[:, [1, 3]] = boxes[:, [1, 3]].clip(0, h)


def _scale_keypoints(kpts: np.ndarray, frame_shape, ratio_pad) -> None:
    gain = float(ratio_pad[0][0])
    pad_x, pad_y = ratio_pad[1]
    kpts[..., 0] = (kpts[..., 0] - float(pad_x)) / gain
    kpts[..., 1] = (kpts[..., 1] - float(pad_y)) / gain
    h, w = frame_shape[:2]
    kpts[..., 0] = kpts[..., 0].clip(0, w)
    kpts[..., 1] = kpts[..., 1].clip(0, h)
