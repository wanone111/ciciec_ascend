"""Small dependency-free MJPEG server for publishing the latest pipeline frame."""

from __future__ import annotations

import threading
import time
import json
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import cv2


class _FrameStore:
    def __init__(self):
        self.condition = threading.Condition()
        self.jpeg = None
        self.sequence = 0

    def update(self, jpeg: bytes):
        with self.condition:
            self.jpeg = jpeg
            self.sequence += 1
            self.condition.notify_all()

    def wait_next(self, sequence: int, timeout: float = 5.0):
        with self.condition:
            self.condition.wait_for(
                lambda: self.sequence != sequence and self.jpeg is not None,
                timeout=timeout,
            )
            return self.sequence, self.jpeg


class MJPEGServer:
    """Publish frames without opening the camera a second time."""

    def __init__(
        self,
        host="0.0.0.0",
        port=8081,
        quality=65,
        max_fps=6.0,
        max_width=640,
    ):
        self.host = host
        self.port = int(port)
        self.quality = max(30, min(int(quality), 95))
        self.max_fps = max(float(max_fps), 0.5)
        self.max_width = max(int(max_width), 0)
        self._store = _FrameStore()
        self._httpd = None
        self._thread = None
        self._encoder_thread = None
        self._pending_condition = threading.Condition()
        self._pending_frame = None
        self._stop_event = threading.Event()
        self._stats_lock = threading.Lock()
        self._frames_submitted = 0
        self._frames_overwritten = 0
        self._frames_encoded = 0
        self._encode_errors = 0
        self._encode_total_ms = 0.0
        self._last_encode_ms = 0.0
        self._last_frame_at = 0.0

    def start(self):
        store = self._store
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path.split("?", 1)[0] == "/health":
                    body = json.dumps(owner.stats()).encode("utf-8")
                    self.send_response(HTTPStatus.OK)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(body)))
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    self.wfile.write(body)
                    return

                if self.path.split("?", 1)[0] == "/snapshot.jpg":
                    _sequence, jpeg = store.wait_next(-1, timeout=3.0)
                    if jpeg is None:
                        self.send_error(HTTPStatus.SERVICE_UNAVAILABLE)
                        return
                    self.send_response(HTTPStatus.OK)
                    self.send_header("Content-Type", "image/jpeg")
                    self.send_header("Content-Length", str(len(jpeg)))
                    self.send_header("Cache-Control", "no-store")
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    self.wfile.write(jpeg)
                    return

                if self.path.split("?", 1)[0] != "/stream.mjpg":
                    self.send_error(HTTPStatus.NOT_FOUND)
                    return

                self.send_response(HTTPStatus.OK)
                self.send_header(
                    "Content-Type", "multipart/x-mixed-replace; boundary=frame"
                )
                self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
                self.send_header("Pragma", "no-cache")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()

                sequence = -1
                try:
                    while True:
                        sequence, jpeg = store.wait_next(sequence)
                        if jpeg is None:
                            continue
                        self.wfile.write(b"--frame\r\n")
                        self.wfile.write(b"Content-Type: image/jpeg\r\n")
                        self.wfile.write(
                            f"Content-Length: {len(jpeg)}\r\n\r\n".encode("ascii")
                        )
                        self.wfile.write(jpeg)
                        self.wfile.write(b"\r\n")
                except (BrokenPipeError, ConnectionResetError):
                    pass

            def log_message(self, _format, *_args):
                return

        self._stop_event.clear()
        self._encoder_thread = threading.Thread(
            target=self._encode_loop,
            name="mjpeg-encoder",
            daemon=True,
        )
        self._encoder_thread.start()
        self._httpd = ThreadingHTTPServer((self.host, self.port), Handler)
        self._thread = threading.Thread(
            target=self._httpd.serve_forever,
            name="mjpeg-server",
            daemon=True,
        )
        self._thread.start()
        print(f"[STREAM] MJPEG listening on http://{self.host}:{self.port}/stream.mjpg")

    def publish(self, frame):
        """Submit the latest frame without JPEG work on the inference thread."""
        if frame is None:
            return False
        with self._pending_condition:
            if self._pending_frame is not None:
                with self._stats_lock:
                    self._frames_overwritten += 1
            self._pending_frame = frame
            with self._stats_lock:
                self._frames_submitted += 1
            self._pending_condition.notify()
        return True

    def _encode_loop(self):
        interval = 1.0 / self.max_fps
        next_encode_at = 0.0
        while not self._stop_event.is_set():
            with self._pending_condition:
                ready = self._pending_condition.wait_for(
                    lambda: self._pending_frame is not None
                    or self._stop_event.is_set(),
                    timeout=1.0,
                )
                if self._stop_event.is_set():
                    break
                # A condition wait can time out or wake spuriously.  In both
                # cases there is no frame to encode, so keep waiting instead
                # of dereferencing None below.
                if not ready or self._pending_frame is None:
                    continue
                frame = self._pending_frame
                self._pending_frame = None

            delay = next_encode_at - time.monotonic()
            if delay > 0 and self._stop_event.wait(delay):
                break

            started = time.perf_counter()
            if self.max_width and frame.shape[1] > self.max_width:
                target_height = round(frame.shape[0] * self.max_width / frame.shape[1])
                frame = cv2.resize(
                    frame,
                    (self.max_width, target_height),
                    interpolation=cv2.INTER_AREA,
                )
            ok, encoded = cv2.imencode(
                ".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, self.quality]
            )
            encode_ms = (time.perf_counter() - started) * 1000.0
            with self._stats_lock:
                self._last_encode_ms = encode_ms
                self._encode_total_ms += encode_ms
                if ok:
                    self._frames_encoded += 1
                    self._last_frame_at = time.time()
                else:
                    self._encode_errors += 1
            if ok:
                self._store.update(encoded.tobytes())
            next_encode_at = time.monotonic() + interval

    def stats(self):
        with self._stats_lock:
            encoded = self._frames_encoded
            age = time.time() - self._last_frame_at if self._last_frame_at else None
            return {
                "status": "ok" if age is not None and age < 3.0 else "waiting",
                "frames_submitted": self._frames_submitted,
                "frames_overwritten": self._frames_overwritten,
                "frames_encoded": encoded,
                "encode_errors": self._encode_errors,
                "last_encode_ms": round(self._last_encode_ms, 3),
                "average_encode_ms": round(self._encode_total_ms / encoded, 3)
                if encoded
                else 0.0,
                "latest_frame_age_seconds": round(age, 3) if age is not None else None,
                "stream_max_fps": self.max_fps,
                "stream_max_width": self.max_width,
                "jpeg_quality": self.quality,
            }

    def close(self):
        self._stop_event.set()
        with self._pending_condition:
            self._pending_condition.notify_all()
        if self._httpd is not None:
            self._httpd.shutdown()
            self._httpd.server_close()
            self._httpd = None
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None
        if self._encoder_thread is not None:
            self._encoder_thread.join(timeout=2.0)
            self._encoder_thread = None
