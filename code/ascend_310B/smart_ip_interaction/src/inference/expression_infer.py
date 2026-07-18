from collections import deque
import os
import time

import cv2
import numpy as np

from inference.acl_om import AclOmSession, prod

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
                 smooth_window=7, use_npu=True, require_npu=True,
                 allow_torch_fallback=False, device_id=0,
                 input_mode="float_nchw"):
        self.device = device
        self.img_size = img_size
        self.num_classes = num_classes
        self.class_names = CLASS_NAMES_6 if num_classes == 6 else CLASS_NAMES_3
        self.use_npu = use_npu
        self.require_npu = require_npu
        self.allow_torch_fallback = allow_torch_fallback
        self.device_id = device_id
        self.model_path = model_path
        if input_mode not in ("float_nchw", "aipp_bgr_u8", "aipp_nv12"):
            raise ValueError(f"unsupported expression input_mode={input_mode!r}")
        self.input_mode = input_mode

        self._net = None
        self._om_session = None
        self._torch_backend = False
        self._torch = None
        self._image_cls = None
        self.transform = None
        self._backend = "uninitialized"
        self.last_profile = {}
        self.fused_preprocess = _env_bool("SMART_IP_EXPRESSION_FUSED_PREPROCESS", True)

        if model_path.lower().endswith(".om") and os.path.exists(model_path):
            if not self.use_npu:
                raise RuntimeError("Refusing to load emotion OM with use_npu=False")
            self._om_session = AclOmSession(
                model_path, device_id=device_id, input_mode=self.input_mode
            )
            self._backend = "acl_om_npu"
            print(f"[EXPR] using ACL OM/NPU: {model_path}")
        elif model_path.lower().endswith(".onnx") and os.path.exists(model_path):
            self._net = cv2.dnn.readNetFromONNX(model_path)
            backend_ready = False
            if self.use_npu and hasattr(cv2.dnn, "DNN_BACKEND_CANN") and hasattr(cv2.dnn, "DNN_TARGET_NPU"):
                try:
                    self._net.setPreferableBackend(cv2.dnn.DNN_BACKEND_CANN)
                    self._net.setPreferableTarget(cv2.dnn.DNN_TARGET_NPU)
                    print(f"[EXPR] using ONNX + CANN/NPU: {model_path}")
                    backend_ready = True
                    self._backend = "opencv_dnn_cann_npu"
                except cv2.error:
                    backend_ready = False

            if not backend_ready:
                if self.require_npu:
                    raise RuntimeError(
                        "Emotion ONNX was selected, but OpenCV DNN CANN/NPU is unavailable. "
                        "Use models/om/emotion_mobilenetv3_6cls.om with ACL."
                    )
                self._net.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
                self._net.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)
                print(f"[EXPR] using ONNX + CPU: {model_path}")
                self._backend = "opencv_dnn_cpu"
        else:
            if self.require_npu or not self.allow_torch_fallback:
                raise RuntimeError(
                    "Torch emotion fallback is disabled for the 310B production path. "
                    "Use the OM model or explicitly enable allow_torch_fallback for local debugging."
                )
            self._torch_backend = True

        if self._torch_backend:
            import torch
            import torch.nn as nn
            from PIL import Image
            from torchvision import models, transforms

            self._torch = torch
            self._image_cls = Image
            self.device = self.device or ("cuda" if torch.cuda.is_available() else "cpu")
            self.model = models.mobilenet_v3_small(weights=None)
            self.model.classifier[3] = nn.Linear(
                self.model.classifier[3].in_features, num_classes
            )
            self.model.load_state_dict(
                torch.load(model_path, map_location=self.device)
            )
            self.model.to(self.device)
            self.model.eval()
            self._backend = "torch"
            self.transform = transforms.Compose([
                transforms.Resize((self.img_size, self.img_size)),
                transforms.ToTensor(),
            ])
        else:
            self.model = None
            self.device = self.device or "cpu"

        # 滑动窗口投票，防止单帧抖动
        self._smooth_window = smooth_window
        self._vote_buffer = deque(maxlen=smooth_window)

    @property
    def backend(self):
        return self._backend

    def runtime_info(self):
        info = {
            "expression_backend": self._backend,
            "expression_model_path": self.model_path,
            "expression_require_npu": self.require_npu,
            "expression_allow_torch_fallback": self.allow_torch_fallback,
            "expression_device_id": self.device_id,
            "expression_input_mode": self.input_mode,
        }
        if self._om_session is not None:
            info.update(
                expression_om_input_dims=self._om_session.input_dims,
                expression_om_input_sizes=self._om_session.input_sizes,
                expression_om_output_dims=self._om_session.output_dims,
                expression_om_output_sizes=self._om_session.output_sizes,
                expression_om_input_names=self._om_session.input_names,
                expression_om_output_names=self._om_session.output_names,
                expression_acl_shared_context=True,
                expression_acl_runtime_refcount=self._om_session.runtime_refcount,
            )
        return info

    # ------------------------------------------------------------------
    # 推理
    # ------------------------------------------------------------------
    def predict_bgr(self, bgr_img):
        """输入 BGR numpy 图像，返回原始单帧结果（不含平滑）。"""
        if self._om_session is not None:
            total_start = time.perf_counter()
            preprocess_start = time.perf_counter()
            input_array = self._preprocess_om(bgr_img)
            preprocess_ms = _elapsed_ms(preprocess_start)
            logits = np.asarray(self._om_session.execute(input_array)[0], dtype=np.float32).reshape(-1)
            postprocess_start = time.perf_counter()
            probs = _softmax(logits)
            pred_idx = int(np.argmax(probs))
            self.last_profile = {
                "preprocess_ms": preprocess_ms,
                "postprocess_ms": _elapsed_ms(postprocess_start),
                "model_total_ms": _elapsed_ms(total_start),
            }
        elif self._net is not None:
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
            pil_img = self._image_cls.fromarray(rgb)
            x = self.transform(pil_img).unsqueeze(0).to(self.device)

            with self._torch.no_grad():
                logits = self.model(x)
                probs = self._torch.softmax(logits, dim=1)[0].cpu().numpy()
                pred_idx = int(probs.argmax())

        return {
            "expression": self.class_names[pred_idx],
            "confidence": float(probs[pred_idx]),
            "probs": np.asarray(probs)
        }

    def _preprocess_om(self, bgr_img):
        if self._om_session is None:
            raise RuntimeError("Emotion OM session is not initialized")
        dims = self._om_session.input_dims[0]
        expected_size = self._om_session.input_sizes[0]
        input_mode = getattr(self, "input_mode", None)
        if input_mode == "aipp_nv12":
            raise RuntimeError("expression aipp_nv12 requires predict_device()")
        if input_mode == "aipp_bgr_u8":
            resized = cv2.resize(
                bgr_img, (self.img_size, self.img_size), interpolation=cv2.INTER_LINEAR
            )
            packed = np.ascontiguousarray(resized, dtype=np.uint8)
            if packed.nbytes != expected_size:
                raise RuntimeError(
                    f"expression AIPP BGR contract mismatch: model={self.model_path}, dims={dims}, "
                    f"expected_bytes={expected_size}, actual_bytes={packed.nbytes}, "
                    f"input_mode={self.input_mode}"
                )
            return packed
        elem_count = prod(dims) if dims else 1 * 3 * self.img_size * self.img_size
        is_nhwc = bool(dims and len(dims) == 4 and dims[-1] in (1, 3))
        if expected_size == elem_count * np.dtype(np.float32).itemsize:
            dtype = np.float32
        elif input_mode == "float_nchw" or expected_size != elem_count:
            raise RuntimeError(
                f"expression float_nchw contract mismatch: dims={dims}, expected_size={expected_size}, "
                f"input_mode={input_mode}"
            )
        else:
            dtype = np.uint8
        if dtype is np.float32 and not is_nhwc and self.fused_preprocess:
            return cv2.dnn.blobFromImage(
                bgr_img,
                scalefactor=1.0 / 255.0,
                size=(self.img_size, self.img_size),
                swapRB=True,
                crop=False,
            )

        rgb = cv2.cvtColor(bgr_img, cv2.COLOR_BGR2RGB)
        rgb = cv2.resize(rgb, (self.img_size, self.img_size), interpolation=cv2.INTER_LINEAR)
        tensor = rgb.astype(dtype)
        if dtype is np.float32:
            tensor /= 255.0
        if is_nhwc:
            tensor = tensor[np.newaxis, ...]
        else:
            tensor = tensor.transpose(2, 0, 1)[np.newaxis, ...]
        return np.ascontiguousarray(tensor, dtype=dtype)

    def predict_device(self, device_image):
        if self.input_mode != "aipp_nv12" or self._om_session is None:
            raise RuntimeError(
                f"expression predict_device requires aipp_nv12, got {self.input_mode}"
            )
        total_start = time.perf_counter()
        logits = np.asarray(
            self._om_session.execute_device_input(device_image.ptr, device_image.size)[0],
            dtype=np.float32,
        ).reshape(-1)
        post_start = time.perf_counter()
        probs = _softmax(logits)
        pred_idx = int(np.argmax(probs))
        self.last_profile = {
            "preprocess_ms": 0.0, "postprocess_ms": _elapsed_ms(post_start),
            "model_total_ms": _elapsed_ms(total_start),
        }
        return {
            "expression": self.class_names[pred_idx],
            "confidence": float(probs[pred_idx]), "probs": probs,
        }

    def predict_smooth_device(self, device_image):
        raw = self.predict_device(device_image)
        self._vote_buffer.append(raw["probs"])
        avg_probs = np.mean(self._vote_buffer, axis=0)
        index = int(avg_probs.argmax())
        return {"expression": self.class_names[index], "confidence": float(avg_probs[index])}

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

    def close(self):
        if self._om_session is not None:
            self._om_session.close()
            self._om_session = None

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


def _softmax(logits):
    logits = np.asarray(logits, dtype=np.float32)
    exp = np.exp(logits - np.max(logits))
    return exp / np.sum(exp)


def _env_bool(name, default):
    raw = os.environ.get(name)
    if raw is None:
        return default
    return raw.strip().lower() not in ("0", "false", "no", "off")


def _elapsed_ms(start):
    return (time.perf_counter() - start) * 1000.0
