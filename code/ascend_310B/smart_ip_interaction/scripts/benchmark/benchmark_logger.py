#!/usr/bin/env python3
"""Compare current async JSONL hot path with a preserved logger implementation."""

from __future__ import annotations

import argparse
import importlib.util
import tempfile
import time
from pathlib import Path

from benchmark.logger import JsonlLogger


def load_logger(path: Path):
    spec = importlib.util.spec_from_file_location("reference_logger", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.JsonlLogger


def measure(name, logger_type, records):
    path = Path(tempfile.gettempdir()) / f"smart_ip_{name}.jsonl"
    path.unlink(missing_ok=True)
    logger = logger_type(path, "pipeline")
    started = time.perf_counter()
    for index in range(records):
        logger.log("frame", frame_idx=index, pose_ms=45.0, frame_total_ms=60.0)
    hot_path_ms = (time.perf_counter() - started) * 1000.0
    logger.close()
    total_ms = (time.perf_counter() - started) * 1000.0
    return {
        "name": name,
        "records": records,
        "hot_path_total_ms": hot_path_ms,
        "hot_path_us_per_log": hot_path_ms * 1000.0 / records,
        "close_inclusive_ms": total_ms,
        "bytes": path.stat().st_size,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--records", type=int, default=2000)
    args = parser.parse_args()
    print(measure("sync_reference", load_logger(args.reference), args.records))
    print(measure("async_current", JsonlLogger, args.records))


if __name__ == "__main__":
    main()
