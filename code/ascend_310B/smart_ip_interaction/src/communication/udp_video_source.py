"""
UDP 视频源适配器
================
将硬件 VideoStreamInterface（pybind11 模块）的异步回调接口
适配成与 cv2.VideoCapture 兼容的同步 read() 接口。

用法::

    src = UDPVideoSource(port=9090, module_path="/path/to/build")
    if src.isOpened():
        while True:
            ok, frame = src.read()   # frame 已转为 BGR，与 cap.read() 等价
            if not ok:
                break
            ...
    src.release()

依赖:
  - video_stream_interface.so  (310B 上 CMake 编译产出的 pybind11 模块)
  - numpy, opencv-python
"""

import queue
import time
import threading
import importlib
import sys
import os
from typing import Optional, Tuple

import cv2
import numpy as np


class UDPVideoSource:
    """cv2.VideoCapture 兼容的 UDP 视频流适配器。"""

    # 与 C++ udp_common.h 保持一致
    PIX_FMT_RGB24 = 1
    PIX_FMT_MJPEG = 2
    PIX_FMT_NV12  = 3

    def __init__(
        self,
        port: int = 9090,
        module_path: Optional[str] = None,
        max_queue: int = 4,
        read_timeout: float = 2.0,
    ):
        """
        Parameters
        ----------
        port        : 监听端口，与 PYNQ_PORT 保持一致
        module_path : video_stream_interface.so 所在目录；None 时用 PYTHONPATH
        max_queue   : 帧队列最大长度（超出时丢弃最旧帧，防止堆积）
        read_timeout: read() 等待新帧的超时秒数
        """
        self._port = port
        self._max_queue = max_queue
        self._read_timeout = read_timeout
        self._frame_queue: queue.Queue = queue.Queue(maxsize=max_queue)
        self._opened = False
        self._interface = None
        self._vsi_mod = None

        # 加载 pybind11 模块
        if module_path and module_path not in sys.path:
            sys.path.insert(0, module_path)

        try:
            self._vsi_mod = importlib.import_module("video_stream_interface")
        except ImportError as e:
            print(f"[UDPVideoSource] 无法加载 video_stream_interface 模块: {e}")
            print("[UDPVideoSource] 请将编译产出的 .so 路径传给 module_path= 参数")
            return

        try:
            self._interface = self._vsi_mod.VideoStreamInterface(port)
            self._interface.set_frame_callback(self._on_frame)
            self._interface.start_background()
            self._opened = True
            print(f"[UDPVideoSource] 已启动，监听端口 {port}")
        except Exception as e:
            print(f"[UDPVideoSource] 启动失败: {e}")

    # ── 回调：在 C++ 线程中调用，把帧放入队列 ─────────────────────
    def _on_frame(self, frame) -> None:
        # 注意：此回调在 C++ epoll 线程中执行，避免复杂 Python 操作
        # 只做数据拷贝，转换推迟到主线程
        try:
            # 队列满时丢弃最旧帧，保证实时性
            if self._frame_queue.full():
                try:
                    self._frame_queue.get_nowait()
                except queue.Empty:
                    pass
            # 直接放入 pybind11 frame 对象，在主线程中转换
            self._frame_queue.put_nowait(frame)
        except queue.Full:
            pass
        except Exception as e:
            print(f"[UDPVideoSource] 回调异常: {e}")

    # ── 像素格式转换 ──────────────────────────────────────────────
    def _to_bgr(self, frame) -> Optional[np.ndarray]:
        data = frame.data
        h, w = frame.height, frame.width
        fmt = frame.pixel_format

        try:
            data = np.asarray(data, dtype=np.uint8)
            if fmt == self.PIX_FMT_RGB24:
                rgb = data.reshape((h, w, 3))
                return cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)

            elif fmt == self.PIX_FMT_MJPEG:
                buf = np.frombuffer(data, dtype=np.uint8)
                bgr = cv2.imdecode(buf, cv2.IMREAD_COLOR)
                return bgr  # imdecode 输出已是 BGR

            elif fmt == self.PIX_FMT_NV12:
                # NV12: Y 平面(h行) + UV 交错(h/2行) → 总高 = h*3/2
                yuv = data.reshape((h * 3 // 2, w))
                return cv2.cvtColor(yuv, cv2.COLOR_YUV2BGR_NV12)

            else:
                print(f"[UDPVideoSource] 未知像素格式: {fmt}")
                return None
        except Exception as e:
            print(f"[UDPVideoSource] 帧转换失败: {e}")
            return None

    # ── 公共接口（与 cv2.VideoCapture 兼容）──────────────────────
    def isOpened(self) -> bool:
        return self._opened

    def read(self) -> Tuple[bool, Optional[np.ndarray]]:
        """阻塞等待下一帧，超时返回 (False, None)。"""
        try:
            frame = self._frame_queue.get(timeout=self._read_timeout)
            # 在主线程中进行 pybind11 对象转换（避免 GIL 冲突）
            bgr = self._to_bgr(frame)
            if bgr is not None:
                return True, bgr
            else:
                return False, None
        except queue.Empty:
            return False, None

    def release(self) -> None:
        if self._interface is not None:
            try:
                self._interface.stop()
            except Exception:
                pass
        self._opened = False
        print("[UDPVideoSource] 已停止")
