"""
Pose inference wrapper for Ascend 310B.

The production path is ACL + OM. ONNX+CANN is kept as an explicit secondary
NPU path, while PT/Ultralytics loading is disabled unless callers opt in.
"""

from __future__ import annotations

import os
import time
from dataclasses import dataclass
from functools import reduce
from operator import mul
from typing import Any

import cv2
import numpy as np


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


class _AclOmSession:
    """Minimal ACL model session for one static-input OM model."""

    def __init__(self, model_path: str, device_id: int = 0):
        try:
            import acl  # type: ignore
        except ImportError as exc:
            raise RuntimeError(
                "Cannot import acl. Source the Ascend CANN environment before "
                "starting the 310B pipeline, for example "
                "`source /usr/local/Ascend/ascend-toolkit/set_env.sh`."
            ) from exc

        self.acl = acl
        self.model_path = model_path
        self.device_id = device_id
        self.context = None
        self.model_id = None
        self.model_desc = None
        self.input_dataset = None
        self.output_dataset = None
        self.input_buffers: list[Any] = []
        self.output_buffers: list[Any] = []
        self.input_device_ptrs: list[Any] = []
        self.output_device_ptrs: list[Any] = []
        self.output_host_ptrs: list[Any] = []
        self.input_sizes: list[int] = []
        self.output_sizes: list[int] = []
        self.input_dims: list[list[int] | None] = []
        self.output_dims: list[list[int] | None] = []
        self.profile_enabled = _env_bool("SMART_IP_ACL_PROFILE", False)
        self.last_profile: dict[str, float] = {}
        self._closed = False

        self._check(self.acl.init(), "acl.init")
        self._check(self.acl.rt.set_device(device_id), "acl.rt.set_device")
        self.context = self._value(self.acl.rt.create_context(device_id), "acl.rt.create_context")
        self._set_context()
        self.model_id = self._value(self.acl.mdl.load_from_file(model_path), "acl.mdl.load_from_file")
        self.model_desc = self.acl.mdl.create_desc()
        self._check(self.acl.mdl.get_desc(self.model_desc, self.model_id), "acl.mdl.get_desc")

        input_count = int(self._value(self.acl.mdl.get_num_inputs(self.model_desc), "acl.mdl.get_num_inputs"))
        output_count = int(self._value(self.acl.mdl.get_num_outputs(self.model_desc), "acl.mdl.get_num_outputs"))
        if input_count != 1:
            raise RuntimeError(f"Expected one pose OM input, got {input_count}")
        if output_count < 1:
            raise RuntimeError("Pose OM has no outputs")

        self.input_sizes = [
            int(self._value(self.acl.mdl.get_input_size_by_index(self.model_desc, i), "acl.mdl.get_input_size_by_index"))
            for i in range(input_count)
        ]
        self.output_sizes = [
            int(self._value(self.acl.mdl.get_output_size_by_index(self.model_desc, i), "acl.mdl.get_output_size_by_index"))
            for i in range(output_count)
        ]
        self.input_dims = [self._get_dims("input", i) for i in range(input_count)]
        self.output_dims = [self._get_dims("output", i) for i in range(output_count)]

        self._create_datasets()

    @staticmethod
    def _check(ret: Any, op: str) -> None:
        if isinstance(ret, tuple):
            ret = ret[-1]
        if op == "acl.init" and ret in (100002, 207015):
            return
        if ret not in (0, None):
            raise RuntimeError(f"{op} failed, ret={ret}")

    @classmethod
    def _value(cls, result: Any, op: str) -> Any:
        if isinstance(result, tuple) and len(result) >= 2:
            cls._check(result[-1], op)
            return result[0]
        return result

    def _set_context(self) -> None:
        if self.context is None:
            return
        set_context = getattr(self.acl.rt, "set_context", None)
        if set_context is not None:
            self._check(set_context(self.context), "acl.rt.set_context")

    def _get_dims(self, kind: str, index: int) -> list[int] | None:
        fn_name = f"get_{kind}_dims"
        fn = getattr(self.acl.mdl, fn_name, None)
        if fn is None:
            return None
        try:
            dims = self._value(fn(self.model_desc, index), f"acl.mdl.{fn_name}")
        except Exception:
            return None
        if isinstance(dims, dict):
            values = dims.get("dims")
            count = dims.get("dimCount") or (len(values) if values is not None else 0)
            if values:
                return [int(v) for v in values[:count]]
        if isinstance(dims, (list, tuple)):
            return [int(v) for v in dims]
        return None

    def _create_datasets(self) -> None:
        self.input_dataset = self.acl.mdl.create_dataset()
        self.output_dataset = self.acl.mdl.create_dataset()

        for size in self.input_sizes:
            ptr = self._value(self.acl.rt.malloc(size, 0), "acl.rt.malloc(input)")
            self.input_device_ptrs.append(ptr)
            buf = self.acl.create_data_buffer(ptr, size)
            self.input_buffers.append(buf)
            self._check(self.acl.mdl.add_dataset_buffer(self.input_dataset, buf), "acl.mdl.add_dataset_buffer(input)")

        for size in self.output_sizes:
            dev_ptr = self._value(self.acl.rt.malloc(size, 0), "acl.rt.malloc(output)")
            host_ptr = self._value(self.acl.rt.malloc_host(size), "acl.rt.malloc_host(output)")
            self.output_device_ptrs.append(dev_ptr)
            self.output_host_ptrs.append(host_ptr)
            buf = self.acl.create_data_buffer(dev_ptr, size)
            self.output_buffers.append(buf)
            self._check(self.acl.mdl.add_dataset_buffer(self.output_dataset, buf), "acl.mdl.add_dataset_buffer(output)")

    def execute(self, input_array: np.ndarray) -> list[np.ndarray]:
        if self._closed:
            raise RuntimeError("ACL OM session is already closed")

        total_start = time.perf_counter()
        self._set_context()
        arr = np.ascontiguousarray(input_array)
        input_bytes = arr.tobytes()
        input_size = self.input_sizes[0]
        if len(input_bytes) != input_size:
            raise RuntimeError(
                f"Pose OM input size mismatch: model expects {input_size} bytes, "
                f"preprocess produced {len(input_bytes)} bytes."
            )

        src_ptr = self.acl.util.bytes_to_ptr(input_bytes)
        h2d_start = time.perf_counter()
        self._check(
            self.acl.rt.memcpy(self.input_device_ptrs[0], input_size, src_ptr, input_size, 1),
            "acl.rt.memcpy(host_to_device)",
        )
        h2d_ms = _elapsed_ms(h2d_start)
        execute_start = time.perf_counter()
        self._check(self.acl.mdl.execute(self.model_id, self.input_dataset, self.output_dataset), "acl.mdl.execute")
        execute_ms = _elapsed_ms(execute_start)

        outputs = []
        d2h_ms = 0.0
        decode_ms = 0.0
        for idx, (dev_ptr, host_ptr, size) in enumerate(zip(self.output_device_ptrs, self.output_host_ptrs, self.output_sizes)):
            d2h_start = time.perf_counter()
            self._check(
                self.acl.rt.memcpy(host_ptr, size, dev_ptr, size, 2),
                "acl.rt.memcpy(device_to_host)",
            )
            d2h_ms += _elapsed_ms(d2h_start)
            decode_start = time.perf_counter()
            raw = self.acl.util.ptr_to_bytes(host_ptr, size)
            arr_out = np.frombuffer(raw, dtype=np.float32).copy()
            dims = self.output_dims[idx]
            if dims:
                elem_count = _prod(dims)
                if elem_count == arr_out.size:
                    arr_out = arr_out.reshape(dims)
            outputs.append(arr_out)
            decode_ms += _elapsed_ms(decode_start)
        if self.profile_enabled:
            self.last_profile = {
                "acl_h2d_ms": h2d_ms,
                "acl_execute_ms": execute_ms,
                "acl_d2h_ms": d2h_ms,
                "acl_output_decode_ms": decode_ms,
                "acl_total_ms": _elapsed_ms(total_start),
            }
        return outputs

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        try:
            self._set_context()
        except Exception:
            pass

        for buf in self.input_buffers + self.output_buffers:
            try:
                self.acl.destroy_data_buffer(buf)
            except Exception:
                pass
        for dataset in (self.input_dataset, self.output_dataset):
            if dataset is not None:
                try:
                    self.acl.mdl.destroy_dataset(dataset)
                except Exception:
                    pass
        for ptr in self.output_host_ptrs:
            try:
                self.acl.rt.free_host(ptr)
            except Exception:
                pass
        for ptr in self.input_device_ptrs + self.output_device_ptrs:
            try:
                self.acl.rt.free(ptr)
            except Exception:
                pass
        if self.model_id is not None:
            try:
                self.acl.mdl.unload(self.model_id)
            except Exception:
                pass
        if self.model_desc is not None:
            try:
                self.acl.mdl.destroy_desc(self.model_desc)
            except Exception:
                pass
        if self.context is not None:
            try:
                self.acl.rt.destroy_context(self.context)
            except Exception:
                pass
        try:
            self.acl.rt.reset_device(self.device_id)
        except Exception:
            pass
        try:
            self.acl.finalize()
        except Exception:
            pass

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


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
        rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        layout = _infer_layout(dims)
        elem_count = _prod(dims) if dims else rgb.size
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
            tensor = rgb.astype(np.float32) / 255.0
        else:
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


def _elapsed_ms(start: float) -> float:
    return (time.perf_counter() - start) * 1000.0


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
