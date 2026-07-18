"""DVPP VPC helpers with explicit resource ownership and pure geometry tests.

The module deliberately imports no ``acl`` package.  A processor receives the
shared runtime owned by :class:`AclOmSession`, so importing this module on a PC
without CANN remains safe.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any
import time

import numpy as np


ACL_MEMCPY_HOST_TO_DEVICE = 1
ACL_MEMCPY_DEVICE_TO_HOST = 2


def _check(ret: Any, op: str) -> None:
    if isinstance(ret, tuple):
        ret = ret[-1]
    if ret not in (0, None):
        raise RuntimeError(f"{op} failed, ret={ret}")


def _value(result: Any, op: str) -> Any:
    if isinstance(result, tuple) and len(result) >= 2:
        _check(result[-1], op)
        return result[0]
    return result


def align_up(value: int, alignment: int) -> int:
    if value <= 0 or alignment <= 0:
        raise ValueError("value and alignment must be positive")
    return ((int(value) + alignment - 1) // alignment) * alignment


def align_width(width: int) -> int:
    """Default 310B YUV420SP output-stride alignment (16 pixels)."""
    return align_up(width, 16)


def align_height(height: int) -> int:
    return align_up(height, 2)


def align_yuv420_roi(
    bbox: tuple[float, float, float, float], width: int, height: int
) -> tuple[int, int, int, int] | None:
    """Return an inclusive DVPP YUV420 ROI (even left/top, odd right/bottom)."""
    if width < 2 or height < 2:
        return None
    x1, y1, x2, y2 = bbox
    left = max(0, min(width - 2, int(np.floor(x1)))) & ~1
    top = max(0, min(height - 2, int(np.floor(y1)))) & ~1
    right_exclusive = max(left + 2, min(width, int(np.ceil(x2))))
    bottom_exclusive = max(top + 2, min(height, int(np.ceil(y2))))
    right = min(width - 1, right_exclusive - 1)
    bottom = min(height - 1, bottom_exclusive - 1)
    if right % 2 == 0:
        right -= 1
    if bottom % 2 == 0:
        bottom -= 1
    if right <= left or bottom <= top:
        return None
    return left, top, right, bottom


@dataclass(frozen=True)
class LetterboxGeometry:
    ratio: float
    pad: tuple[float, float]
    resized: tuple[int, int]
    borders: tuple[int, int, int, int]  # left, top, right, bottom


def pose_letterbox_geometry(width: int, height: int, size: int = 640) -> LetterboxGeometry:
    """Match PoseInfer._letterbox's round and +/-0.1 border rules."""
    if min(width, height, size) <= 0:
        raise ValueError("width, height and size must be positive")
    ratio = min(size / height, size / width)
    new_w, new_h = int(round(width * ratio)), int(round(height * ratio))
    dw, dh = (size - new_w) / 2.0, (size - new_h) / 2.0
    left, right = int(round(dw - 0.1)), int(round(dw + 0.1))
    top, bottom = int(round(dh - 0.1)), int(round(dh + 0.1))
    return LetterboxGeometry(ratio, (dw, dh), (new_w, new_h), (left, top, right, bottom))


@dataclass(frozen=True)
class FaceCanvasGeometry:
    scale: float
    resized: tuple[int, int]
    paste_roi: tuple[int, int, int, int]


def face_canvas_geometry(width: int, height: int, model_size: tuple[int, int] = (640, 640)) -> FaceCanvasGeometry:
    if min(width, height, *model_size) <= 0:
        raise ValueError("dimensions must be positive")
    model_w, model_h = model_size
    scale = min(model_w / width, model_h / height)
    new_w, new_h = max(1, int(round(width * scale))), max(1, int(round(height * scale)))
    # Inclusive VPC ROI; force the output edge to legal YUV420 odd coordinates.
    right = max(1, new_w - 1)
    bottom = max(1, new_h - 1)
    right = right if right % 2 else right - 1
    bottom = bottom if bottom % 2 else bottom - 1
    return FaceCanvasGeometry(scale, (new_w, new_h), (0, 0, right, bottom))


def make_nv12_canvas(width_stride: int, height_stride: int, y_value: int, uv_value: int = 128) -> np.ndarray:
    size = width_stride * height_stride * 3 // 2
    data = np.empty(size, dtype=np.uint8)
    y_size = width_stride * height_stride
    data[:y_size].fill(np.uint8(y_value))
    data[y_size:].fill(np.uint8(uv_value))
    return data


def _bgr_to_nv12_fullrange_reference(frame: np.ndarray) -> np.ndarray:
    """NumPy reference conversion using the PYNQ HLS coefficients."""
    if frame.ndim != 3 or frame.shape[2] != 3 or frame.dtype != np.uint8:
        raise ValueError("expected HxWx3 uint8 BGR frame")
    h, w = frame.shape[:2]
    if h % 2 or w % 2:
        raise ValueError("NV12 requires even width and height")
    f = frame.astype(np.float32)
    b, g, r = f[..., 0], f[..., 1], f[..., 2]
    y = np.clip(np.rint(0.299 * r + 0.587 * g + 0.114 * b), 0, 255).astype(np.uint8)
    u = np.clip(np.rint(-0.169 * r - 0.331 * g + 0.500 * b + 128), 0, 255)
    v = np.clip(np.rint(0.500 * r - 0.419 * g - 0.081 * b + 128), 0, 255)
    u2 = np.rint(u.reshape(h // 2, 2, w // 2, 2).mean(axis=(1, 3))).astype(np.uint8)
    v2 = np.rint(v.reshape(h // 2, 2, w // 2, 2).mean(axis=(1, 3))).astype(np.uint8)
    uv = np.empty((h // 2, w), dtype=np.uint8)
    uv[:, 0::2], uv[:, 1::2] = u2, v2
    return np.concatenate((y.reshape(-1), uv.reshape(-1)))


def bgr_to_nv12_fullrange(frame: np.ndarray) -> np.ndarray:
    """Fast transitional CPU conversion, within one code value of the HLS formula."""
    if frame.ndim != 3 or frame.shape[2] != 3 or frame.dtype != np.uint8:
        raise ValueError("expected HxWx3 uint8 BGR frame")
    h, w = frame.shape[:2]
    if h % 2 or w % 2:
        raise ValueError("NV12 requires even width and height")

    import cv2

    # BGRA supplies a constant alpha=255, allowing the +128 chroma bias to be
    # included in one SIMD-accelerated transform.  Coefficients exactly follow
    # video_params.h; OpenCV saturation/rounding can differ by at most one LSB
    # from the NumPy reference and is covered by a regression test.
    bgra = cv2.cvtColor(np.ascontiguousarray(frame), cv2.COLOR_BGR2BGRA)
    matrix = np.array(
        [
            (0.114, 0.587, 0.299, 0.0),
            (0.500, -0.331, -0.169, 128.0 / 255.0),
            (-0.081, -0.419, 0.500, 128.0 / 255.0),
        ],
        dtype=np.float32,
    )
    yuv444 = cv2.transform(bgra, matrix)
    u = cv2.resize(yuv444[..., 1], (w // 2, h // 2), interpolation=cv2.INTER_AREA)
    v = cv2.resize(yuv444[..., 2], (w // 2, h // 2), interpolation=cv2.INTER_AREA)
    output = np.empty(h * w * 3 // 2, dtype=np.uint8)
    output[: h * w] = yuv444[..., 0].reshape(-1)
    uv = output[h * w :].reshape(h // 2, w)
    uv[:, 0::2], uv[:, 1::2] = u, v
    return output


@dataclass
class DvppImage:
    ptr: Any
    size: int
    width: int
    height: int
    width_stride: int
    height_stride: int
    pixel_format: int
    owner: bool = True


class DvppProcessor:
    """Reusable DVPP channel/buffers bound to an existing shared ACL runtime."""

    REQUIRED_APIS = (
        "dvpp_malloc", "dvpp_free", "dvpp_create_channel_desc",
        "dvpp_create_channel", "dvpp_destroy_channel",
        "dvpp_vpc_crop_and_paste_async", "dvpp_create_pic_desc",
        "dvpp_destroy_pic_desc", "dvpp_create_roi_config",
        "dvpp_destroy_roi_config", "dvpp_create_resize_config",
        "dvpp_destroy_resize_config",
    )

    def __init__(self, shared_runtime: Any, device_id: int = 0):
        self.runtime = shared_runtime
        self.acl = shared_runtime.acl
        self.media = getattr(self.acl, "media", None)
        if self.media is None:
            raise RuntimeError("acl.media is unavailable; DVPP cannot be enabled")
        self.device_id = int(device_id)
        self.runtime.set_context()
        self.capabilities = {name: hasattr(self.media, name) for name in self.REQUIRED_APIS}
        missing = [name for name, present in self.capabilities.items() if not present]
        if missing:
            raise RuntimeError("DVPP APIs missing in this CANN Python binding: " + ", ".join(missing))
        self.stream = self._value(self.acl.rt.create_stream(), "acl.rt.create_stream")
        self.channel_desc = self.media.dvpp_create_channel_desc()
        self._check(self.media.dvpp_create_channel(self.channel_desc), "acl.media.dvpp_create_channel")
        self._buffers: dict[str, DvppImage] = {}
        self._closed = False
        self.last_profile: dict[str, float | int] = {}

    @staticmethod
    def _check(ret: Any, op: str) -> None:
        _check(ret, op)

    @staticmethod
    def _value(result: Any, op: str) -> Any:
        return _value(result, op)

    def _pixel_format_nv12(self) -> int:
        for owner in (self.media, self.acl):
            for name in ("PIXEL_FORMAT_YUV_SEMIPLANAR_420", "PIXEL_FORMAT_YUV_420_SP"):
                if hasattr(owner, name):
                    return int(getattr(owner, name))
        # CANN 7 board binding does not export enum attributes.  The installed
        # acl_dvpp.h was audited and defines PIXEL_FORMAT_YUV_SEMIPLANAR_420=1.
        return 1

    def _get_buffer(self, key: str, width: int, height: int, pixel_format: int) -> DvppImage:
        ws, hs = align_width(width), align_height(height)
        size = ws * hs * 3 // 2
        old = self._buffers.get(key)
        if old is not None and (old.size, old.width, old.height, old.pixel_format) == (size, width, height, pixel_format):
            return old
        if old is not None:
            self._free_image(old)
        ptr = self._value(self.media.dvpp_malloc(size), "acl.media.dvpp_malloc")
        image = DvppImage(ptr, size, width, height, ws, hs, pixel_format, True)
        self._buffers[key] = image
        return image

    def upload_nv12(self, data: np.ndarray, width: int, height: int, *, key: str = "source") -> DvppImage:
        if width % 2 or height % 2:
            raise ValueError(f"NV12 requires even dimensions, got {width}x{height}")
        flat = np.ascontiguousarray(data, dtype=np.uint8).reshape(-1)
        expected = width * height * 3 // 2
        if flat.size != expected:
            raise ValueError(f"NV12 size mismatch: expected={expected}, actual={flat.size}, size={width}x{height}")
        image = self._get_buffer(key, width, height, self._pixel_format_nv12())
        if image.width_stride == width and image.height_stride == height:
            packed = flat
        else:
            packed = make_nv12_canvas(image.width_stride, image.height_stride, 0, 128)
            y_src = flat[: width * height].reshape(height, width)
            uv_src = flat[width * height :].reshape(height // 2, width)
            packed[: image.width_stride * image.height_stride].reshape(image.height_stride, image.width_stride)[:height, :width] = y_src
            packed[image.width_stride * image.height_stride :].reshape(image.height_stride // 2, image.width_stride)[: height // 2, :width] = uv_src
        start = time.perf_counter()
        src_ptr = self.acl.util.bytes_to_ptr(packed.tobytes())
        self._check(self.acl.rt.memcpy(image.ptr, image.size, src_ptr, image.size, ACL_MEMCPY_HOST_TO_DEVICE), "acl.rt.memcpy(host_to_dvpp)")
        self.last_profile = {"dvpp_h2d_ms": (time.perf_counter() - start) * 1000.0, "dvpp_h2d_bytes": image.size}
        return image

    def upload_bgr_or_rgb(self, frame: np.ndarray, *, rgb: bool = False, key: str = "source") -> DvppImage:
        bgr = frame[..., ::-1] if rgb else frame
        convert_start = time.perf_counter()
        packed = bgr_to_nv12_fullrange(bgr)
        convert_ms = (time.perf_counter() - convert_start) * 1000.0
        image = self.upload_nv12(packed, frame.shape[1], frame.shape[0], key=key)
        self.last_profile["dvpp_bgr_to_nv12_ms"] = convert_ms
        return image

    def download_nv12(self, image: DvppImage) -> np.ndarray:
        """Validation-only D2H helper; the production pipeline never calls it."""
        host_ptr = self._value(self.acl.rt.malloc_host(image.size), "acl.rt.malloc_host(dvpp_validation)")
        try:
            self._check(self.acl.rt.memcpy(host_ptr, image.size, image.ptr, image.size, ACL_MEMCPY_DEVICE_TO_HOST), "acl.rt.memcpy(dvpp_to_host_validation)")
            return np.frombuffer(self.acl.util.ptr_to_bytes(host_ptr, image.size), dtype=np.uint8).copy()
        finally:
            self.acl.rt.free_host(host_ptr)

    def _fill_nv12(self, image: DvppImage, y_value: int, uv_value: int = 128) -> None:
        y_size = image.width_stride * image.height_stride
        self._check(self.acl.rt.memset(image.ptr, y_size, int(y_value), y_size), "acl.rt.memset(nv12_y)")
        self._check(self.acl.rt.memset(image.ptr + y_size, image.size - y_size, int(uv_value), image.size - y_size), "acl.rt.memset(nv12_uv)")

    def _pic_desc(self, image: DvppImage) -> Any:
        desc = self.media.dvpp_create_pic_desc()
        setters = {
            "dvpp_set_pic_desc_data": image.ptr,
            "dvpp_set_pic_desc_size": image.size,
            "dvpp_set_pic_desc_format": image.pixel_format,
            "dvpp_set_pic_desc_width": image.width,
            "dvpp_set_pic_desc_height": image.height,
            "dvpp_set_pic_desc_width_stride": image.width_stride,
            "dvpp_set_pic_desc_height_stride": image.height_stride,
        }
        try:
            for name, value in setters.items():
                fn = getattr(self.media, name, None)
                if fn is None:
                    raise RuntimeError(f"acl.media.{name} is unavailable")
                self._check(fn(desc, value), f"acl.media.{name}")
            return desc
        except Exception:
            self.media.dvpp_destroy_pic_desc(desc)
            raise

    def _crop_paste(self, source: DvppImage, output: DvppImage, crop: tuple[int, int, int, int], paste: tuple[int, int, int, int]) -> None:
        in_desc, out_desc = self._pic_desc(source), self._pic_desc(output)
        crop_roi = self.media.dvpp_create_roi_config(crop[0], crop[2], crop[1], crop[3])
        paste_roi = self.media.dvpp_create_roi_config(paste[0], paste[2], paste[1], paste[3])
        resize = self.media.dvpp_create_resize_config()
        start = time.perf_counter()
        try:
            operation = self.media.dvpp_vpc_crop_and_paste_async
            try:
                ret = operation(
                    self.channel_desc, in_desc, out_desc, crop_roi, paste_roi,
                    resize, self.stream,
                )
                api_variant = "seven_args_with_resize_config"
            except TypeError as exc:
                # CANN 7.0.RC1 Python binding on the target 310B exposes the
                # documented six-argument form without a resize config.
                try:
                    ret = operation(
                        self.channel_desc, in_desc, out_desc, crop_roi,
                        paste_roi, self.stream,
                    )
                    api_variant = "cann_7_six_args"
                except TypeError:
                    raise RuntimeError(
                        "unsupported dvpp_vpc_crop_and_paste_async signature; "
                        f"seven-argument error was: {exc}"
                    ) from exc
            self._check(ret, "acl.media.dvpp_vpc_crop_and_paste_async")
            self._check(self.acl.rt.synchronize_stream(self.stream), "acl.rt.synchronize_stream")
            self.last_profile["dvpp_vpc_ms"] = (time.perf_counter() - start) * 1000.0
            self.last_profile["dvpp_crop_paste_api"] = api_variant
        finally:
            for obj, destroy in ((resize, self.media.dvpp_destroy_resize_config), (paste_roi, self.media.dvpp_destroy_roi_config), (crop_roi, self.media.dvpp_destroy_roi_config), (out_desc, self.media.dvpp_destroy_pic_desc), (in_desc, self.media.dvpp_destroy_pic_desc)):
                try:
                    destroy(obj)
                except Exception:
                    pass

    def make_pose_letterbox(self, source: DvppImage, size: int = 640) -> tuple[DvppImage, LetterboxGeometry]:
        requested = pose_letterbox_geometry(source.width, source.height, size)
        output = self._get_buffer("pose", size, size, source.pixel_format)
        self._fill_nv12(output, 114, 128)
        left, top, _right_pad, _bottom_pad = requested.borders
        new_w, new_h = requested.resized
        source_roi = align_yuv420_roi((0, 0, source.width, source.height), source.width, source.height)
        paste_roi = align_yuv420_roi((left, top, left + new_w, top + new_h), size, size)
        if source_roi is None or paste_roi is None:
            raise RuntimeError("invalid pose DVPP ROI")
        self._crop_paste(source, output, source_roi, paste_roi)
        actual_left, actual_top, actual_right, actual_bottom = paste_roi
        actual = LetterboxGeometry(
            ratio=requested.ratio,
            pad=(float(actual_left), float(actual_top)),
            resized=(actual_right - actual_left + 1, actual_bottom - actual_top + 1),
            borders=(
                actual_left, actual_top, size - actual_right - 1,
                size - actual_bottom - 1,
            ),
        )
        self.last_profile["pose_requested_pad"] = requested.pad
        self.last_profile["pose_actual_pad"] = actual.pad
        self.last_profile["pose_alignment_delta"] = (
            actual.pad[0] - requested.pad[0], actual.pad[1] - requested.pad[1]
        )
        return output, actual

    def make_face_canvas(self, source: DvppImage, size: tuple[int, int] = (640, 640)) -> tuple[DvppImage, FaceCanvasGeometry]:
        geometry = face_canvas_geometry(source.width, source.height, size)
        output = self._get_buffer("face", size[0], size[1], source.pixel_format)
        self._fill_nv12(output, 0, 128)
        source_roi = align_yuv420_roi((0, 0, source.width, source.height), source.width, source.height)
        if source_roi is None:
            raise RuntimeError("invalid face source ROI")
        self._crop_paste(source, output, source_roi, geometry.paste_roi)
        return output, geometry

    def crop_expression(self, source: DvppImage, bbox: tuple[float, float, float, float], size: int = 112) -> tuple[DvppImage, dict[str, Any]] | None:
        aligned = align_yuv420_roi(bbox, source.width, source.height)
        if aligned is None:
            return None
        output = self._get_buffer("expression", size, size, source.pixel_format)
        self._fill_nv12(output, 0, 128)
        paste = (0, 0, size - 1 if (size - 1) % 2 else size - 2, size - 1 if (size - 1) % 2 else size - 2)
        self._crop_paste(source, output, aligned, paste)
        left, top, right, bottom = aligned
        meta = {
            "original_bbox": tuple(float(v) for v in bbox),
            "aligned_bbox": aligned,
            "scale": (size / (right - left + 1), size / (bottom - top + 1)),
        }
        return output, meta

    def _free_image(self, image: DvppImage) -> None:
        if image.owner and image.ptr not in (None, 0):
            try:
                self.media.dvpp_free(image.ptr)
            finally:
                image.owner = False
                image.ptr = None

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        for image in list(self._buffers.values()):
            try:
                self._free_image(image)
            except Exception:
                pass
        self._buffers.clear()
        if self.channel_desc is not None:
            try:
                self.media.dvpp_destroy_channel(self.channel_desc)
            except Exception:
                pass
            try:
                self.media.dvpp_destroy_channel_desc(self.channel_desc)
            except Exception:
                pass
            self.channel_desc = None
        if self.stream is not None:
            try:
                self.acl.rt.destroy_stream(self.stream)
            except Exception:
                pass
            self.stream = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
