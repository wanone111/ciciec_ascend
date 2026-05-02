"""
TCP 推送客户端（可选连接版）
==============================
- 连不上后端时不崩溃，自动重连
- 发送失败静默丢弃，不阻塞推理主线程
- 后台线程维护连接，主线程只管 put 数据
"""

import socket
import json
import threading
import queue
import time


class TCPSender:
    def __init__(self, host="127.0.0.1", port=9000, retry_interval=3.0):
        self.host = host
        self.port = port
        self.retry_interval = retry_interval

        self._sock = None
        self._lock = threading.Lock()
        self._queue = queue.Queue(maxsize=30)  # 最多缓存30条，满了丢弃旧的
        self._connected = False
        self._stop = False

        # 后台线程：维护连接 + 发送
        self._thread = threading.Thread(target=self._worker, daemon=True)
        self._thread.start()

    # ── 公共接口 ──────────────────────────────────────────────
    def send(self, data: dict):
        """非阻塞发送，队列满时丢弃最旧的一条。"""
        msg = (json.dumps(data, ensure_ascii=False) + "\n").encode("utf-8")
        if self._queue.full():
            try:
                self._queue.get_nowait()
            except queue.Empty:
                pass
        try:
            self._queue.put_nowait(msg)
        except queue.Full:
            pass

    @property
    def connected(self):
        return self._connected

    def close(self):
        self._stop = True
        self._disconnect()

    # ── 内部 ──────────────────────────────────────────────────
    def _worker(self):
        while not self._stop:
            if not self._connected:
                self._try_connect()
                if not self._connected:
                    time.sleep(self.retry_interval)
                    continue

            try:
                msg = self._queue.get(timeout=1.0)
            except queue.Empty:
                continue

            try:
                with self._lock:
                    self._sock.sendall(msg)
            except Exception:
                self._disconnect()
                # 把消息放回去，下次连上再发
                try:
                    self._queue.put_nowait(msg)
                except queue.Full:
                    pass

    def _try_connect(self):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2.0)
            sock.connect((self.host, self.port))
            sock.settimeout(None)
            with self._lock:
                self._sock = sock
                self._connected = True
            print(f"[TCP] 已连接 {self.host}:{self.port}")
        except Exception:
            self._connected = False

    def _disconnect(self):
        with self._lock:
            if self._sock:
                try:
                    self._sock.close()
                except Exception:
                    pass
                self._sock = None
            self._connected = False
        print(f"[TCP] 连接断开，{self.retry_interval}s 后重试")
