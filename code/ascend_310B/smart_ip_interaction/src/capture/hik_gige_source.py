"""OpenCV-compatible source for Hikrobot GigE Vision industrial cameras."""

from __future__ import annotations

import importlib
import os
import socket
import struct
import sys
import threading
import time
import fcntl
from ctypes import POINTER, cast, c_ubyte, sizeof, byref, memset
from typing import Any, Dict, Optional, Tuple

import cv2
import numpy as np


def _decode_c_string(value: Any) -> str:
    return bytes(value).split(b"\0", 1)[0].decode("utf-8", "replace")


def _ip4(value: int) -> str:
    value = int(value)
    return ".".join(str((value >> shift) & 0xFF) for shift in (24, 16, 8, 0))


def _interface_mtu_for_ip(host_ip: str) -> Optional[int]:
    """Return the MTU of the interface owning *host_ip* on Linux."""
    if not host_ip:
        return None
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        for _index, name in socket.if_nameindex():
            request = struct.pack("256s", name[:15].encode("utf-8"))
            try:
                address = socket.inet_ntoa(
                    fcntl.ioctl(sock.fileno(), 0x8915, request)[20:24]
                )
            except OSError:
                continue
            if address != host_ip:
                continue
            result = fcntl.ioctl(sock.fileno(), 0x8921, request)
            return int(struct.unpack_from("i", result, 16)[0])
    finally:
        sock.close()
    return None


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
        latest_frame: bool = True,
        acquisition_fps: float = 0.0,
    ) -> None:
        self.camera_ip = camera_ip.strip()
        self.serial_number = serial_number.strip()
        self.device_index = max(0, int(device_index))
        self.timeout_ms = max(1, int(timeout_ms))
        self.sdk_root = sdk_root
        self.auto_exposure = bool(auto_exposure)
        self.auto_gain = bool(auto_gain)
        self.latest_frame = bool(latest_frame)
        self.acquisition_fps = max(0.0, float(acquisition_fps))

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
        self._delivered = 0
        self._overwritten = 0
        self._latest_condition = threading.Condition()
        self._latest_sequence = 0
        self._delivered_sequence = 0
        self._latest_frame = None
        self._latest_metadata: Dict[str, Any] = {}
        self._capture_stop = threading.Event()
        self._capture_thread = None
        self.last_error = ""

        try:
            self._open()
            if self.latest_frame:
                self._capture_thread = threading.Thread(
                    target=self._capture_loop,
                    name="hik-gige-capture",
                    daemon=True,
                )
                self._capture_thread.start()
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

        optimal_packet_size = int(self._camera.MV_CC_GetOptimalPacketSize())
        host_mtu = _interface_mtu_for_ip(self._selected.get("host_ip", ""))
        configured_packet_size = int(os.getenv("SMART_IP_HIK_PACKET_SIZE", "0"))
        if configured_packet_size > 0:
            packet_size = configured_packet_size
        elif host_mtu:
            # MVS may report a jumbo packet size based on the NIC capability
            # even when the currently configured interface MTU is only 1500.
            packet_size = min(optimal_packet_size, host_mtu)
        else:
            packet_size = optimal_packet_size
        if packet_size > 0:
            self._check(
                self._camera.MV_CC_SetIntValue("GevSCPSPacketSize", packet_size),
                "Hikrobot set packet size",
            )
            self._selected["packet_size"] = packet_size
            self._selected["optimal_packet_size"] = optimal_packet_size
            self._selected["host_mtu"] = host_mtu

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
        if self.acquisition_fps > 0:
            self._check(
                self._camera.MV_CC_SetBoolValue("AcquisitionFrameRateEnable", True),
                "Hikrobot enable acquisition frame rate",
            )
            self._check(
                self._camera.MV_CC_SetFloatValue(
                    "AcquisitionFrameRate", self.acquisition_fps
                ),
                "Hikrobot set acquisition frame rate",
            )
            self._selected["acquisition_fps_target"] = self.acquisition_fps

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

    def _grab_one(
        self, *, defer_color_conversion: bool = False
    ) -> Tuple[bool, Optional[np.ndarray], Dict[str, Any]]:
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
            info = frame.stFrameInfo
            pixel_type = int(info.enPixelType)
            deferred_color = None
            if defer_color_conversion and pixel_type == int(
                self._mvs.PixelType_Gvsp_RGB8_Packed
            ):
                length = int(info.nWidth) * int(info.nHeight) * 3
                image = self._raw_array(frame.pBufAddr, info.nFrameLen)[:length]
                image = image.reshape(int(info.nHeight), int(info.nWidth), 3).copy()
                deferred_color = "rgb8"
            elif defer_color_conversion and pixel_type == int(
                self._mvs.PixelType_Gvsp_Mono8
            ):
                length = int(info.nWidth) * int(info.nHeight)
                image = self._raw_array(frame.pBufAddr, info.nFrameLen)[:length]
                image = image.reshape(int(info.nHeight), int(info.nWidth)).copy()
                deferred_color = "mono8"
            else:
                image = self._to_bgr(frame)
            self._frames += 1
            self._lost_packets += int(info.nLostPacket)
            metadata = {
                "source": "hik_gige",
                "frame_id": int(info.nFrameNum),
                "timestamp_us": time.time_ns() // 1000,
                "captured_monotonic_ns": time.monotonic_ns(),
                "width": int(info.nWidth),
                "height": int(info.nHeight),
                "pixel_type": int(info.enPixelType),
                "frame_length": int(info.nFrameLen),
                "lost_packets": int(info.nLostPacket),
                "camera_ip": self._selected.get("camera_ip"),
                "serial_number": self._selected.get("serial_number"),
            }
            if deferred_color:
                metadata["_deferred_color"] = deferred_color
            return True, image, metadata
        except Exception as exc:
            self.last_error = str(exc)
            return False, None, {"source": "hik_gige", "error": self.last_error}
        finally:
            self._camera.MV_CC_FreeImageBuffer(frame)

    def _capture_loop(self) -> None:
        while not self._capture_stop.is_set() and self.isOpened():
            ok, image, metadata = self._grab_one(defer_color_conversion=True)
            if not ok:
                if self._capture_stop.wait(0.01):
                    break
                continue
            with self._latest_condition:
                if self._latest_sequence != self._delivered_sequence:
                    self._overwritten += 1
                self._latest_frame = image
                self._latest_metadata = metadata
                self._latest_sequence += 1
                self._latest_condition.notify_all()

    def read_with_info(self) -> Tuple[bool, Optional[np.ndarray], Dict[str, Any]]:
        if not self.latest_frame:
            ok, image, metadata = self._grab_one()
            if ok:
                self._delivered += 1
            return ok, image, metadata
        if not self.isOpened():
            return False, None, {"source": "hik_gige", "error": self.last_error}
        deadline = time.monotonic() + self.timeout_ms / 1000.0
        with self._latest_condition:
            while self._latest_sequence == self._delivered_sequence:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return False, None, {
                        "source": "hik_gige",
                        "error": "latest-frame wait timeout",
                    }
                self._latest_condition.wait(timeout=remaining)
            image = self._latest_frame
            metadata = dict(self._latest_metadata)
            self._delivered_sequence = self._latest_sequence
            self._delivered += 1
        deferred_color = metadata.pop("_deferred_color", None)
        if deferred_color == "rgb8":
            image = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)
        elif deferred_color == "mono8":
            image = cv2.cvtColor(image, cv2.COLOR_GRAY2BGR)
        metadata["delivered_monotonic_ns"] = time.monotonic_ns()
        metadata["capture_age_ms"] = (
            metadata["delivered_monotonic_ns"]
            - metadata.get("captured_monotonic_ns", metadata["delivered_monotonic_ns"])
        ) / 1_000_000.0
        return image is not None, image, metadata

    def read(self) -> Tuple[bool, Optional[np.ndarray]]:
        ok, frame, _ = self.read_with_info()
        return ok, frame

    def get_detailed_stats(self) -> Dict[str, Any]:
        elapsed = max(time.monotonic() - self._started_at, 1e-6)
        return {
            "source": "hik_gige",
            "frames": self._frames,
            "frames_delivered": self._delivered,
            "frames_overwritten": self._overwritten,
            "timeouts": self._timeouts,
            "lost_packets": self._lost_packets,
            "capture_fps": self._frames / elapsed,
            "delivery_fps": self._delivered / elapsed,
            "latest_frame_mode": self.latest_frame,
            "last_error": self.last_error,
            **self.device_info,
        }

    def release(self) -> None:
        self._capture_stop.set()
        with self._latest_condition:
            self._latest_condition.notify_all()
        if self._capture_thread is not None:
            self._capture_thread.join(timeout=self.timeout_ms / 1000.0 + 1.0)
            self._capture_thread = None
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
