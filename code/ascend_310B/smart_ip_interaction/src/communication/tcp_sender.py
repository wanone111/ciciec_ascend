"""Non-blocking TCP JSON sender with bounded latest-event buffering."""

from __future__ import annotations

import json
import queue
import socket
import threading
import time
from typing import Any


class TCPSender:
    def __init__(self, host="127.0.0.1", port=9000, retry_interval=3.0,
                 queue_size=30, message_ttl=5.0):
        self.host = host
        self.port = int(port)
        self.retry_interval = float(retry_interval)
        self.message_ttl = max(0.0, float(message_ttl))

        self._sock = None
        self._lock = threading.Lock()
        self._stats_lock = threading.Lock()
        self._queue: queue.Queue[tuple[float, bytes]] = queue.Queue(
            maxsize=max(1, int(queue_size))
        )
        self._connected = False
        self._stop_event = threading.Event()
        self._enqueued = 0
        self._sent = 0
        self._dropped_overflow = 0
        self._dropped_expired = 0
        self._send_errors = 0
        self._reconnects = 0
        self._send_total_ms = 0.0
        self._queue_age_total_ms = 0.0
        self._last_send_ms = 0.0
        self._last_queue_age_ms = 0.0

        self._thread = threading.Thread(
            target=self._worker, name="tcp-sender", daemon=True
        )
        self._thread.start()

    def send(self, data: dict[str, Any]) -> bool:
        """Enqueue JSON without blocking; discard the oldest item on overflow."""
        msg = (json.dumps(data, ensure_ascii=False) + "\n").encode("utf-8")
        item = (time.monotonic(), msg)
        with self._stats_lock:
            self._enqueued += 1
        if self._queue.full():
            try:
                self._queue.get_nowait()
                self._queue.task_done()
                with self._stats_lock:
                    self._dropped_overflow += 1
            except queue.Empty:
                pass
        try:
            self._queue.put_nowait(item)
            return True
        except queue.Full:
            with self._stats_lock:
                self._dropped_overflow += 1
            return False

    @property
    def connected(self) -> bool:
        return self._connected

    def stats(self) -> dict[str, Any]:
        with self._stats_lock:
            sent = self._sent
            return {
                "connected": self._connected,
                "enqueued": self._enqueued,
                "sent": sent,
                "dropped_overflow": self._dropped_overflow,
                "dropped_expired": self._dropped_expired,
                "send_errors": self._send_errors,
                "reconnects": self._reconnects,
                "queued": self._queue.qsize(),
                "last_send_ms": round(self._last_send_ms, 3),
                "average_send_ms": round(self._send_total_ms / sent, 3) if sent else 0.0,
                "last_queue_age_ms": round(self._last_queue_age_ms, 3),
                "average_queue_age_ms": round(self._queue_age_total_ms / sent, 3) if sent else 0.0,
            }

    def close(self) -> None:
        self._stop_event.set()
        self._disconnect(log=False)
        self._thread.join(timeout=2.0)

    def _worker(self) -> None:
        while not self._stop_event.is_set():
            if not self._connected:
                self._try_connect()
                if not self._connected:
                    self._stop_event.wait(self.retry_interval)
                    continue

            try:
                queued_at, msg = self._queue.get(timeout=0.5)
            except queue.Empty:
                continue

            age_ms = (time.monotonic() - queued_at) * 1000.0
            if self.message_ttl and age_ms > self.message_ttl * 1000.0:
                with self._stats_lock:
                    self._dropped_expired += 1
                self._queue.task_done()
                continue

            started = time.perf_counter()
            try:
                with self._lock:
                    sock = self._sock
                    if sock is None:
                        raise ConnectionError("TCP socket is closed")
                    sock.sendall(msg)
                send_ms = (time.perf_counter() - started) * 1000.0
                with self._stats_lock:
                    self._sent += 1
                    self._send_total_ms += send_ms
                    self._queue_age_total_ms += age_ms
                    self._last_send_ms = send_ms
                    self._last_queue_age_ms = age_ms
            except Exception:
                with self._stats_lock:
                    self._send_errors += 1
                self._disconnect()
                try:
                    self._queue.put_nowait((queued_at, msg))
                except queue.Full:
                    with self._stats_lock:
                        self._dropped_overflow += 1
            finally:
                self._queue.task_done()

    def _try_connect(self) -> None:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2.0)
            sock.connect((self.host, self.port))
            sock.settimeout(None)
            with self._lock:
                self._sock = sock
                was_connected = self._connected
                self._connected = True
            if not was_connected:
                with self._stats_lock:
                    self._reconnects += 1
            print(f"[TCP] connected to {self.host}:{self.port}")
        except Exception:
            self._connected = False

    def _disconnect(self, log=True) -> None:
        with self._lock:
            sock, self._sock = self._sock, None
            was_connected = self._connected
            self._connected = False
        if sock is not None:
            try:
                sock.close()
            except Exception:
                pass
        if log and was_connected:
            print(f"[TCP] disconnected; retrying in {self.retry_interval}s")
