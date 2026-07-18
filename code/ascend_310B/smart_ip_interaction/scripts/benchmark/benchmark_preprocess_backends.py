#!/usr/bin/env python3
"""Run real Hik GigE pipeline variants and summarize measured JSONL latency/FPS."""

from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def percentile(values, p):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, round((len(ordered) - 1) * p))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", action="append", required=True, help="NAME=JSON object of SMART_IP_* environment overrides")
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    results = []
    for spec in args.variant:
        name, sep, raw = spec.partition("=")
        if not sep:
            raise SystemExit("--variant must be NAME={JSON}")
        overrides = json.loads(raw)
        run_dir = (args.output_dir / name).resolve()
        run_dir.mkdir(parents=True, exist_ok=True)
        log_path = run_dir / "pipeline.jsonl"
        if log_path.exists():
            log_path.unlink()
        env = os.environ.copy()
        env.update({str(key): str(value) for key, value in overrides.items()})
        env.update({
            "SMART_IP_USE_UDP": "0", "SMART_IP_VIDEO_BACKEND": "hik_gige",
            "SMART_IP_MAX_FRAMES": str(args.frames), "SMART_IP_310B_BENCH_DIR": str(run_dir),
            "SMART_IP_ENABLE_OVERLAY": "0", "SMART_IP_ENABLE_AR": "0",
            "SMART_IP_KEEP_BGR_FOR_DISPLAY": env.get("SMART_IP_KEEP_BGR_FOR_DISPLAY", "0"),
        })
        started = time.perf_counter()
        process = subprocess.run([sys.executable, str(ROOT / "run_pipeline.py")], cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        wall = time.perf_counter() - started
        (run_dir / "stdout.log").write_text(process.stdout, encoding="utf-8")
        records = []
        if log_path.exists():
            records = [json.loads(line) for line in log_path.read_text(encoding="utf-8").splitlines() if line.strip()]
        frames = [item for item in records if item.get("kind") == "frame"]
        totals = [float(item["frame_total_ms"]) for item in frames if item.get("frame_total_ms") is not None]
        fps = [float(item["fps"]) for item in frames if item.get("fps") is not None]
        results.append({
            "name": name, "status": "measured_on_board" if process.returncode == 0 else "failed",
            "returncode": process.returncode, "wall_seconds": wall, "frames_logged": len(frames),
            "frame_total_ms_mean": statistics.fmean(totals) if totals else None,
            "frame_total_ms_p95": percentile(totals, 0.95),
            "fps_mean": statistics.fmean(fps) if fps else None,
            "stdout": str(run_dir / "stdout.log"), "jsonl": str(log_path), "environment": overrides,
        })
    report = {"status": "measured_on_board", "camera": "hik_gige", "variants": results}
    (args.output_dir / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if any(item["returncode"] != 0 for item in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
