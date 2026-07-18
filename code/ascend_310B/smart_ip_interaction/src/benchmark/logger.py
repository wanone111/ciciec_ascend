"""Small JSONL logger used by real-hardware benchmark runs.

Each component writes one JSON object per line so the run can be summarized
after the hardware test. Component-specific defaults match the three-board
deployment layout and can be overridden with environment variables.
"""

from __future__ import annotations

import json
import os
import queue
import threading
import time
from pathlib import Path
from typing import Any


DEFAULT_BENCH_DIRS = {
    "pipeline": "/root/userdata/ciciec/ascend_310B/log",
    "backend": "/home/bb/competition_ws/ciciec_ascend/log",
    "player": "/home/bb/competition_ws/ciciec_ascend/log",
}


class JsonlLogger:
    def __init__(self, path: str | os.PathLike[str] | None, component: str):
        self.component = component
        self.path = Path(path) if path else None
        self._lock = threading.Lock()
        self._fh = None
        self._queue: queue.Queue[dict[str, Any]] = queue.Queue(
            maxsize=max(64, int(os.environ.get("SMART_IP_BENCH_QUEUE_SIZE", "4096")))
        )
        self._flush_interval = max(
            0.05, float(os.environ.get("SMART_IP_BENCH_FLUSH_INTERVAL", "0.5"))
        )
        self._stop_event = threading.Event()
        self._thread = None
        self._written = 0
        self._dropped = 0

        if self.path:
            try:
                self.path.parent.mkdir(parents=True, exist_ok=True)
                self._fh = self.path.open("a", encoding="utf-8")
                self._thread = threading.Thread(
                    target=self._writer_loop,
                    name=f"jsonl-{component}",
                    daemon=True,
                )
                self._thread.start()
            except OSError as exc:
                print(f"[BENCH] disable {component} log at {self.path}: {exc}")
                self.path = None

    @property
    def enabled(self) -> bool:
        return self._fh is not None

    @classmethod
    def from_env(cls, component: str, filename: str | None = None) -> "JsonlLogger":
        env_names = [
            f"SMART_IP_{component.upper()}_BENCH_DIR",
            "SMART_IP_BENCH_DIR",
        ]
        if component == "pipeline":
            env_names.insert(0, "SMART_IP_310B_BENCH_DIR")
        if component in ("backend", "player"):
            env_names.insert(0, "SMART_IP_PC_BENCH_DIR")

        bench_dir = next((os.environ.get(name) for name in env_names if os.environ.get(name)), None)
        if not bench_dir:
            bench_dir = DEFAULT_BENCH_DIRS.get(component)
        if not bench_dir:
            return cls(None, component)
        safe_name = filename or f"{component}.jsonl"
        return cls(Path(bench_dir) / safe_name, component)

    def log(self, kind: str, **payload: Any) -> None:
        if self._fh is None:
            return

        now = time.time()
        record = {
            "component": self.component,
            "kind": kind,
            "ts": now,
            "ts_ns": time.time_ns(),
            "monotonic_ns": time.monotonic_ns(),
        }
        record.update(payload)
        try:
            self._queue.put_nowait(record)
        except queue.Full:
            try:
                self._queue.get_nowait()
                self._queue.task_done()
                self._dropped += 1
            except queue.Empty:
                pass
            try:
                self._queue.put_nowait(record)
            except queue.Full:
                self._dropped += 1

    def _writer_loop(self) -> None:
        next_flush = time.monotonic() + self._flush_interval
        while not self._stop_event.is_set() or not self._queue.empty():
            timeout = max(0.0, next_flush - time.monotonic())
            try:
                record = self._queue.get(timeout=min(timeout, 0.25))
            except queue.Empty:
                record = None
            if record is not None:
                try:
                    line = json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n"
                    with self._lock:
                        if self._fh is not None:
                            self._fh.write(line)
                            self._written += 1
                finally:
                    self._queue.task_done()
            if time.monotonic() >= next_flush:
                with self._lock:
                    if self._fh is not None:
                        self._fh.flush()
                next_flush = time.monotonic() + self._flush_interval

    def stats(self) -> dict[str, int]:
        return {
            "written": self._written,
            "dropped": self._dropped,
            "queued": self._queue.qsize(),
        }

    def close(self) -> None:
        if self._fh is None:
            return
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=5.0)
            self._thread = None
        with self._lock:
            if self._fh is not None:
                self._fh.flush()
                self._fh.close()
                self._fh = None


def time_ms(start_perf: float, end_perf: float | None = None) -> float:
    """Return elapsed milliseconds from a perf_counter timestamp."""
    end = time.perf_counter() if end_perf is None else end_perf
    return (end - start_perf) * 1000.0
