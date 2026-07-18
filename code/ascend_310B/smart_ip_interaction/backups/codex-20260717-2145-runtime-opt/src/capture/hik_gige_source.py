"""OpenCV-compatible source for Hikrobot GigE Vision industrial cameras."""

from __future__ import annotations

import importlib
import os
import sys
import time
from ctypes import POINTER, cast, c_ubyte, sizeof, byref, memset
from typing import Any, Dict, Optional, Tuple

import cv2
import numpy as np


def _decode_c_string(value: Any) -> str:
    return bytes(value).split(b"\0", 1)[0].decode("utf-8", "replace")


def _ip4(value: int) -> str:
    value = int(value)
    return ".".join(str((value >> shift) & 0xFF) for shift in (24, 16, 8, 0))


class HikGigESource:
    """Expose a Hikrobot MVS camera through ``VideoCapture``-style methods."""

    def __init__(
        self,
        *,
        camera_ip: str = "",
        serial_number: str = "",
        device_index: int = 0,
        timeout_ms: int = 1000,
        sdk_root: str = "/opt/MVS",
        auto_exposure: bool = True,
        auto_gain: bool = True,
    ) -> None:
        self.camera_ip = camera_ip.strip()
        self.serial_number = serial_number.strip()
        self.device_index = max(0, int(device_index))
        self.timeout_ms = max(1, int(timeout_ms))
        self.sdk_root = sdk_root
        self.auto_exposure = bool(auto_exposure)
        self.auto_gain = bool(auto_gain)

        self._mvs = None
        self._camera = None
        self._opened = False
        self._grabbing = False
        self._sdk_initialized = False
        self._convert_buffer = None
        self._convert_buffer_size = 0
        self._selected: Dict[str, Any] = {}
        self._frames = 0
        self._timeouts = 0
        self._lost_packets = 0
        self._started_at = time.monotonic()
        self.last_error = ""

        try:
            self._open()
        except Exception as exc:
            self.last_error = str(exc)
            self.release()
            raise

    def _load_sdk(self):
        lib_root = os.path.join(self.sdk_root, "lib")
        os.environ.setdefault("MVCAM_SDK_PATH", self.sdk_root)
        os.environ.setdefault("MVCAM_COMMON_RUNENV", lib_root)
        os.environ.setdefault(
            "MVCAM_GENICAM_CLPROTOCOL", os.path.join(lib_root, "CLProtocol")
        )
        os.environ.setdefault("ALLUSERSPROFILE", os.path.join(self.sdk_root, "MVFG"))
        import_path = os.path.join(
            self.sdk_root, "Samples", "aarch64", "Python", "MvImport"
        )
        if import_path not in sys.path:
            sys.path.insert(0, import_path)
        return importlib.import_module("MvCameraControl_class")

    @staticmethod
    def _check(ret: int, operation: str) -> None:
        if int(ret) != 0:
            raise RuntimeError(f"{operation} failed: 0x{int(ret) & 0xFFFFFFFF:08x}")

    def _describe_device(self, device: Any) -> Dict[str, Any]:
        info = device.SpecialInfo.stGigEInfo
        return {
            "model": _decode_c_string(info.chModelName),
            "serial_number": _decode_c_string(info.chSerialNumber),
            "user_name": _decode_c_string(info.chUserDefinedName),
            "camera_ip": _ip4(info.nCurrentIp),
            "subnet_mask": _ip4(info.nCurrentSubNetMask),
            "gateway": _ip4(info.nDefultGateWay),
            "host_ip": _ip4(info.nNetExport),
        }

    def _select_device(self, devices: Any) -> Tuple[Any, Dict[str, Any]]:
        matches = []
        for index in range(int(devices.nDeviceNum)):
            device = cast(
                devices.pDeviceInfo[index], POINTER(self._mvs.MV_CC_DEVICE_INFO)
            ).contents
            description = self._describe_device(device)
            if self.camera_ip and description["camera_ip"] != self.camera_ip:
                continue
            if self.serial_number and description["serial_number"] != self.serial_number:
                continue
            matches.append((device, description))

        if not matches:
            requested = self.camera_ip or self.serial_number or f"index {self.device_index}"
            raise RuntimeError(f"no matching Hikrobot GigE camera ({requested})")
        if self.device_index >= len(matches):
            raise RuntimeError(
                f"Hikrobot device index {self.device_index} out of range ({len(matches)})"
            )
        return matches[self.device_index]

    def _open(self) -> None:
        self._mvs = self._load_sdk()
        self._check(self._mvs.MvCamera.MV_CC_Initialize(), "MVS initialize")
        self._sdk_initialized = True

        devices = self._mvs.MV_CC_DEVICE_INFO_LIST()
        self._check(
            self._mvs.MvCamera.MV_CC_EnumDevices(self._mvs.MV_GIGE_DEVICE, devices),
            "Hikrobot GigE enumerate",
        )
        if int(devices.nDeviceNum) == 0:
            raise RuntimeError("no Hikrobot GigE camera discovered")

        device, self._selected = self._select_device(devices)
        self._camera = self._mvs.MvCamera()
        self._check(self._camera.MV_CC_CreateHandle(device), "Hikrobot create handle")
        self._check(
            self._camera.MV_CC_OpenDevice(self._mvs.MV_ACCESS_Exclusive, 0),
            "Hikrobot open device",
        )
        self._opened = True

        packet_size = int(self._camera.MV_CC_GetOptimalPacketSize())
        if packet_size > 0:
            self._check(
                self._camera.MV_CC_SetIntValue("GevSCPSPacketSize", packet_size),
                "Hikrobot set packet size",
            )
            self._selected["packet_size"] = packet_size

        self._check(
            self._camera.MV_CC_SetEnumValue(
                "TriggerMode", self._mvs.MV_TRIGGER_MODE_OFF
            ),
            "Hikrobot disable trigger mode",
        )
        if self.auto_exposure:
            self._check(
                self._camera.MV_CC_SetEnumValue(
                    "ExposureAuto", self._mvs.MV_EXPOSURE_AUTO_MODE_CONTINUOUS
                ),
                "Hikrobot enable auto exposure",
            )
        if self.auto_gain:
            self._check(
                self._camera.MV_CC_SetEnumValueByString("GainAuto", "Continuous"),
                "Hikrobot enable auto gain",
            )

        self._check(self._camera.MV_CC_StartGrabbing(), "Hikrobot start grabbing")
        self._grabbing = True

    def isOpened(self) -> bool:  # noqa: N802 - matches cv2.VideoCapture
        return self._opened and self._grabbing

    @property
    def device_info(self) -> Dict[str, Any]:
        return dict(self._selected)

    def _raw_array(self, address: Any, length: int) -> np.ndarray:
        pointer = cast(address, POINTER(c_ubyte))
        return np.ctypeslib.as_array(pointer, shape=(int(length),))

    def _ensure_convert_buffer(self, size: int) -> None:
        if self._convert_buffer is None or self._convert_buffer_size < size:
            self._convert_buffer = (c_ubyte * int(size))()
            self._convert_buffer_size = int(size)

    def _to_bgr(self, frame: Any) -> np.ndarray:
        info = frame.stFrameInfo
        height, width = int(info.nHeight), int(info.nWidth)
        pixel_type = int(info.enPixelType)
        required = width * height

        if pixel_type == int(self._mvs.PixelType_Gvsp_RGB8_Packed):
            rgb = self._raw_array(frame.pBufAddr, info.nFrameLen)[: required * 3]
            return cv2.cvtColor(rgb.reshape(height, width, 3), cv2.COLOR_RGB2BGR)
        if pixel_type == int(self._mvs.PixelType_Gvsp_BGR8_Packed):
            bgr = self._raw_array(frame.pBufAddr, info.nFrameLen)[: required * 3]
            return bgr.reshape(height, width, 3).copy()
        if pixel_type == int(self._mvs.PixelType_Gvsp_Mono8):
            mono = self._raw_array(frame.pBufAddr, info.nFrameLen)[:required]
            return cv2.cvtColor(mono.reshape(height, width), cv2.COLOR_GRAY2BGR)

        destination_size = required * 3
        self._ensure_convert_buffer(destination_size)
        params = self._mvs.MV_CC_PIXEL_CONVERT_PARAM_EX()
        memset(byref(params), 0, sizeof(params))
        params.nWidth = width
        params.nHeight = height
        params.pSrcData = frame.pBufAddr
        params.nSrcDataLen = info.nFrameLen
        params.enSrcPixelType = info.enPixelType
        params.enDstPixelType = self._mvs.PixelType_Gvsp_RGB8_Packed
        params.pDstBuffer = self._convert_buffer
        params.nDstBufferSize = self._convert_buffer_size
        self._check(self._camera.MV_CC_ConvertPixelTypeEx(params), "pixel conversion")
        rgb = np.ctypeslib.as_array(self._convert_buffer)[: params.nDstLen]
        return cv2.cvtColor(rgb.reshape(height, width, 3), cv2.COLOR_RGB2BGR)

    def read_with_info(self) -> Tuple[bool, Optional[np.ndarray], Dict[str, Any]]:
        if not self.isOpened():
            return False, None, {"source": "hik_gige", "error": self.last_error}

        frame = self._mvs.MV_FRAME_OUT()
        memset(byref(frame), 0, sizeof(frame))
        ret = int(self._camera.MV_CC_GetImageBuffer(frame, self.timeout_ms))
        if ret != 0 or not frame.pBufAddr:
            self._timeouts += 1
            self.last_error = f"grab timeout/error: 0x{ret & 0xFFFFFFFF:08x}"
            return False, None, {"source": "hik_gige", "error": self.last_error}

        try:
            image = self._to_bgr(frame)
            info = frame.stFrameInfo
            self._frames += 1
            self._lost_packets += int(info.nLostPacket)
            metadata = {
                "source": "hik_gige",
                "frame_id": int(info.nFrameNum),
                "timestamp_us": time.time_ns() // 1000,
                "width": int(info.nWidth),
                "height": int(info.nHeight),
                "pixel_type": int(info.enPixelType),
                "frame_length": int(info.nFrameLen),
                "lost_packets": int(info.nLostPacket),
                "camera_ip": self._selected.get("camera_ip"),
                "serial_number": self._selected.get("serial_number"),
            }
            return True, image, metadata
        except Exception as exc:
            self.last_error = str(exc)
            return False, None, {"source": "hik_gige", "error": self.last_error}
        finally:
            self._camera.MV_CC_FreeImageBuffer(frame)

    def read(self) -> Tuple[bool, Optional[np.ndarray]]:
        ok, frame, _ = self.read_with_info()
        return ok, frame

    def get_detailed_stats(self) -> Dict[str, Any]:
        elapsed = max(time.monotonic() - self._started_at, 1e-6)
        return {
            "source": "hik_gige",
            "frames": self._frames,
            "timeouts": self._timeouts,
            "lost_packets": self._lost_packets,
            "capture_fps": self._frames / elapsed,
            "last_error": self.last_error,
            **self.device_info,
        }

    def release(self) -> None:
        camera, self._camera = self._camera, None
        if camera is not None:
            if self._grabbing:
                try:
                    camera.MV_CC_StopGrabbing()
                except Exception:
                    pass
            if self._opened:
                try:
                    camera.MV_CC_CloseDevice()
                except Exception:
                    pass
            try:
                camera.MV_CC_DestroyHandle()
            except Exception:
                pass
        self._grabbing = False
        self._opened = False
        if self._sdk_initialized and self._mvs is not None:
            try:
                self._mvs.MvCamera.MV_CC_Finalize()
            except Exception:
                pass
        self._sdk_initialized = False
