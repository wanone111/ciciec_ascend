"""Small JSONL logger used by real-hardware benchmark runs.

Each component writes one JSON object per line so the run can be summarized
after the hardware test. Component-specific defaults match the three-board
deployment layout and can be overridden with environment variables.
"""

from __future__ import annotations

import json
import os
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

        if self.path:
            try:
                self.path.parent.mkdir(parents=True, exist_ok=True)
                self._fh = self.path.open("a", encoding="utf-8")
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
        }
        record.update(payload)

        with self._lock:
            self._fh.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")
            self._fh.flush()

    def close(self) -> None:
        if self._fh is None:
            return
        with self._lock:
            self._fh.close()
            self._fh = None


def time_ms(start_perf: float, end_perf: float | None = None) -> float:
    """Return elapsed milliseconds from a perf_counter timestamp."""
    end = time.perf_counter() if end_perf is None else end_perf
    return (end - start_perf) * 1000.0
