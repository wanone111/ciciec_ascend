"""
YuNet face detector.

Production on Ascend 310B uses ACL + OM. The OpenCV FaceDetectorYN path is kept
only as an explicit non-production fallback.
"""

from __future__ import annotations

import os
import time

import cv2
import numpy as np

from inference.acl_om import AclOmSession, prod


class FaceDetector:
    def __init__(
        self,
        model_path,
        score_threshold=0.6,
        nms_threshold=0.3,
        use_npu=True,
        require_npu=True,
        allow_opencv_fallback=False,
        device_id=0,
        input_size=(640, 640),
        top_k=5,
        input_mode="float_nchw",
    ):
        self.model_path = model_path
        self.score_threshold = score_threshold
        self.nms_threshold = nms_threshold
        self.use_npu = use_npu
        self.require_npu = require_npu
        self.allow_opencv_fallback = allow_opencv_fallback
        self.device_id = device_id
        self.input_size = tuple(input_size)
        self.top_k = top_k
        if input_mode not in ("float_nchw", "aipp_bgr_u8", "aipp_nv12"):
            raise ValueError(f"unsupported face input_mode={input_mode!r}")
        self.input_mode = input_mode
        self._last_input_size = self.input_size
        self.detector = None
        self._om_session = None
        self._backend = "uninitialized"
        self.last_profile = {}
        self.reuse_input_buffer = _env_bool("SMART_IP_REUSE_INPUT_BUFFER", True)
        self._input_canvas = None
        self._om_input_buffer = None

        if model_path.lower().endswith(".om"):
            if not self.use_npu:
                raise RuntimeError("Refusing to load face OM with use_npu=False")
            self._om_session = AclOmSession(
                model_path, device_id=device_id, input_mode=self.input_mode
            )
            self._backend = "acl_om_npu"
            print(f"[FACE] using ACL OM/NPU: {model_path}")
        else:
            if self.require_npu and not self.allow_opencv_fallback:
                raise RuntimeError(
                    "Face detector fallback is disabled for the 310B production path. "
                    "Use models/om/face_detection_yunet_2023mar_640.om."
                )
            self.detector = cv2.FaceDetectorYN.create(
                model=model_path,
                config="",
                input_size=(320, 320),
                score_threshold=score_threshold,
                nms_threshold=nms_threshold,
                top_k=top_k,
            )
            self._last_input_size = (320, 320)
            self._backend = "opencv_facedetectoryn"
            print(f"[FACE] using OpenCV FaceDetectorYN fallback: {model_path}")

    @property
    def backend(self):
        return self._backend

    def runtime_info(self):
        info = {
            "face_backend": self._backend,
            "face_model_path": self.model_path,
            "face_require_npu": self.require_npu,
            "face_allow_opencv_fallback": self.allow_opencv_fallback,
            "face_device_id": self.device_id,
            "face_input_mode": self.input_mode,
        }
        if self._om_session is not None:
            info.update(
                face_om_input_dims=self._om_session.input_dims,
                face_om_input_sizes=self._om_session.input_sizes,
                face_om_output_dims=self._om_session.output_dims,
                face_om_output_sizes=self._om_session.output_sizes,
                face_om_input_names=self._om_session.input_names,
                face_om_output_names=self._om_session.output_names,
                face_acl_shared_context=True,
                face_acl_runtime_refcount=self._om_session.runtime_refcount,
            )
        return info

    def detect_best(self, frame):
        """
        检测帧内所有人脸，返回置信度最高的那个。
        返回值：(roi_bgr, (x1,y1,x2,y2)) 或 (None, None)
        """
        faces = self._detect_om(frame) if self._om_session is not None else self._detect_opencv(frame)
        if not faces:
            return None, None

        best = max(faces, key=lambda item: item[4])
        h, w = frame.shape[:2]
        x1, y1, x2, y2 = [int(round(v)) for v in best[:4]]

        fw = max(0, x2 - x1)
        fh = max(0, y2 - y1)
        pad = int(max(fw, fh) * 0.15)
        x1 = max(0, x1 - pad)
        y1 = max(0, y1 - pad)
        x2 = min(w, x2 + pad)
        y2 = min(h, y2 + pad)

        roi = frame[y1:y2, x1:x2]
        if roi.size == 0:
            return None, None
        return roi, (x1, y1, x2, y2)

    def detect_all(self, frame):
        """返回所有人脸的 bbox 列表 [(x1,y1,x2,y2), ...]。"""
        faces = self._detect_om(frame) if self._om_session is not None else self._detect_opencv(frame)
        return [(int(f[0]), int(f[1]), int(f[2]), int(f[3])) for f in faces]

    def _detect_opencv(self, frame):
        h, w = frame.shape[:2]
        if self._last_input_size != (w, h):
            self.detector.setInputSize((w, h))
            self._last_input_size = (w, h)

        _, faces = self.detector.detect(frame)
        if faces is None or len(faces) == 0:
            return []

        out = []
        for f in faces:
            x, y, fw, fh, score = float(f[0]), float(f[1]), float(f[2]), float(f[3]), float(f[-1])
            out.append((x, y, x + fw, y + fh, score))
        return out

    def _detect_om(self, frame):
        total_start = time.perf_counter()
        preprocess_start = time.perf_counter()
        input_tensor, scale = self._preprocess_om(frame)
        preprocess_ms = _elapsed_ms(preprocess_start)
        outputs = self._om_session.execute(input_tensor)
        postprocess_start = time.perf_counter()
        heads = _reshape_yunet_outputs(outputs)
        faces = _decode_yunet(
            heads,
            input_size=self.input_size,
            score_threshold=self.score_threshold,
            nms_threshold=self.nms_threshold,
            top_k=self.top_k,
        )
        if not faces:
            self.last_profile = {
                "preprocess_ms": preprocess_ms,
                "postprocess_ms": _elapsed_ms(postprocess_start),
                "model_total_ms": _elapsed_ms(total_start),
            }
            return []

        h, w = frame.shape[:2]
        scaled = []
        for x1, y1, x2, y2, score in faces:
            x1, x2 = x1 / scale, x2 / scale
            y1, y2 = y1 / scale, y2 / scale
            x1 = float(np.clip(x1, 0, w))
            x2 = float(np.clip(x2, 0, w))
            y1 = float(np.clip(y1, 0, h))
            y2 = float(np.clip(y2, 0, h))
            if x2 > x1 and y2 > y1:
                scaled.append((x1, y1, x2, y2, score))
        self.last_profile = {
            "preprocess_ms": preprocess_ms,
            "postprocess_ms": _elapsed_ms(postprocess_start),
            "model_total_ms": _elapsed_ms(total_start),
        }
        return scaled

    def detect_all_device(self, device_image, source_shape, scale):
        if self.input_mode != "aipp_nv12" or self._om_session is None:
            raise RuntimeError(f"face device input requires aipp_nv12, got {self.input_mode}")
        total_start = time.perf_counter()
        outputs = self._om_session.execute_device_input(device_image.ptr, device_image.size)
        post_start = time.perf_counter()
        faces = _decode_yunet(
            _reshape_yunet_outputs(outputs), input_size=self.input_size,
            score_threshold=self.score_threshold, nms_threshold=self.nms_threshold,
            top_k=self.top_k,
        )
        h, w = source_shape[:2]
        mapped = []
        for x1, y1, x2, y2, score in faces:
            x1, x2 = float(np.clip(x1 / scale, 0, w)), float(np.clip(x2 / scale, 0, w))
            y1, y2 = float(np.clip(y1 / scale, 0, h)), float(np.clip(y2 / scale, 0, h))
            if x2 > x1 and y2 > y1:
                mapped.append((x1, y1, x2, y2, score))
        self.last_profile = {
            "preprocess_ms": 0.0, "postprocess_ms": _elapsed_ms(post_start),
            "model_total_ms": _elapsed_ms(total_start),
        }
        return mapped

    def _preprocess_om(self, frame):
        if self._om_session is None:
            raise RuntimeError("Face OM session is not initialized")

        model_w, model_h = self.input_size
        h, w = frame.shape[:2]
        scale = min(model_w / max(w, 1), model_h / max(h, 1))
        new_w, new_h = int(round(w * scale)), int(round(h * scale))
        resized = cv2.resize(frame, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
        reuse_input_buffer = getattr(self, "reuse_input_buffer", False)
        if reuse_input_buffer:
            canvas_shape = (model_h, model_w, 3)
            if self._input_canvas is None or self._input_canvas.shape != canvas_shape:
                self._input_canvas = np.empty(canvas_shape, dtype=np.uint8)
            canvas = self._input_canvas
            canvas.fill(0)
        else:
            canvas = np.zeros((model_h, model_w, 3), dtype=np.uint8)
        canvas[:new_h, :new_w] = resized

        dims = self._om_session.input_dims[0]
        expected_size = self._om_session.input_sizes[0]
        input_mode = getattr(self, "input_mode", None)
        if input_mode == "aipp_nv12":
            raise RuntimeError("face aipp_nv12 requires detect_all_device()")
        if input_mode == "aipp_bgr_u8":
            packed = np.ascontiguousarray(canvas, dtype=np.uint8)
            if packed.nbytes != expected_size:
                raise RuntimeError(
                    f"face AIPP BGR contract mismatch: model={self.model_path}, dims={dims}, "
                    f"expected_bytes={expected_size}, actual_bytes={packed.nbytes}, "
                    f"input_mode={self.input_mode}"
                )
            return packed, scale
        elem_count = prod(dims) if dims else 1 * 3 * model_h * model_w
        if expected_size == elem_count * np.dtype(np.float32).itemsize:
            dtype = np.float32
        elif input_mode == "float_nchw" or expected_size != elem_count:
            raise RuntimeError(
                f"face float_nchw contract mismatch: dims={dims}, expected_size={expected_size}, "
                f"input_mode={input_mode}"
            )
        else:
            dtype = np.uint8

        is_nhwc = bool(dims and len(dims) == 4 and dims[-1] in (1, 3))
        if dtype is np.float32 and not is_nhwc and reuse_input_buffer:
            shape = tuple(dims) if dims else (1, 3, model_h, model_w)
            if self._om_input_buffer is None or self._om_input_buffer.shape != shape:
                self._om_input_buffer = np.empty(shape, dtype=np.float32)
            tensor = self._om_input_buffer
            np.copyto(tensor[0, 0], canvas[:, :, 0], casting="unsafe")
            np.copyto(tensor[0, 1], canvas[:, :, 1], casting="unsafe")
            np.copyto(tensor[0, 2], canvas[:, :, 2], casting="unsafe")
            return tensor, scale

        tensor = canvas.astype(dtype)
        if is_nhwc:
            tensor = tensor[np.newaxis, ...]
        else:
            tensor = tensor.transpose(2, 0, 1)[np.newaxis, ...]
        return np.ascontiguousarray(tensor, dtype=dtype), scale

    def close(self):
        if self._om_session is not None:
            self._om_session.close()
            self._om_session = None


def _reshape_yunet_outputs(outputs):
    expected = {
        "cls_8": (1, 6400, 1),
        "cls_16": (1, 1600, 1),
        "cls_32": (1, 400, 1),
        "obj_8": (1, 6400, 1),
        "obj_16": (1, 1600, 1),
        "obj_32": (1, 400, 1),
        "bbox_8": (1, 6400, 4),
        "bbox_16": (1, 1600, 4),
        "bbox_32": (1, 400, 4),
        "kps_8": (1, 6400, 10),
        "kps_16": (1, 1600, 10),
        "kps_32": (1, 400, 10),
    }
    names = list(expected)
    if len(outputs) != len(names):
        raise RuntimeError(f"YuNet OM expected 12 outputs, got {len(outputs)}")

    result = {}
    for name, arr in zip(names, outputs):
        arr = np.asarray(arr, dtype=np.float32)
        shape = expected[name]
        if arr.shape != shape:
            if arr.size != prod(shape):
                raise RuntimeError(f"YuNet output {name} shape mismatch: got {arr.shape}, expected {shape}")
            arr = arr.reshape(shape)
        result[name] = arr[0]
    return result


def _env_bool(name, default):
    raw = os.environ.get(name)
    if raw is None:
        return default
    return raw.strip().lower() not in ("0", "false", "no", "off")


def _elapsed_ms(start):
    return (time.perf_counter() - start) * 1000.0


def _decode_yunet(heads, input_size, score_threshold, nms_threshold, top_k=5):
    model_w, model_h = input_size
    boxes = []
    scores = []

    for stride in (8, 16, 32):
        cls = heads[f"cls_{stride}"].reshape(-1)
        obj = heads[f"obj_{stride}"].reshape(-1)
        bbox = heads[f"bbox_{stride}"].reshape(-1, 4)
        kps = heads[f"kps_{stride}"].reshape(-1, 10)
        cols = model_w // stride
        rows = model_h // stride

        score = np.sqrt(np.clip(cls, 0.0, 1.0) * np.clip(obj, 0.0, 1.0))
        keep = np.where(score >= score_threshold)[0]
        for idx in keep:
            row = idx // cols
            col = idx % cols
            b = bbox[idx]
            cx = (col + b[0]) * stride
            cy = (row + b[1]) * stride
            bw = np.exp(b[2]) * stride
            bh = np.exp(b[3]) * stride
            x1 = cx - bw / 2.0
            y1 = cy - bh / 2.0
            x2 = cx + bw / 2.0
            y2 = cy + bh / 2.0

            # Keep landmark decode here to match YuNet math even though this
            # pipeline currently only needs bbox coordinates.
            _ = [
                ((col + kps[idx][2 * k]) * stride, (row + kps[idx][2 * k + 1]) * stride)
                for k in range(5)
            ]
            boxes.append((x1, y1, x2, y2))
            scores.append(float(score[idx]))

    if not boxes:
        return []

    boxes_np = np.asarray(boxes, dtype=np.float32)
    scores_np = np.asarray(scores, dtype=np.float32)
    keep = _nms_xyxy(boxes_np, scores_np, nms_threshold, max_det=top_k)
    return [
        (float(boxes_np[i, 0]), float(boxes_np[i, 1]), float(boxes_np[i, 2]), float(boxes_np[i, 3]), float(scores_np[i]))
        for i in keep
    ]


def _nms_xyxy(boxes, scores, iou_threshold, max_det=50):
    x1, y1, x2, y2 = boxes.T
    areas = np.maximum(0.0, x2 - x1) * np.maximum(0.0, y2 - y1)
    order = scores.argsort()[::-1]
    keep = []

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
        order = rest[iou <= iou_threshold]

    return keep
