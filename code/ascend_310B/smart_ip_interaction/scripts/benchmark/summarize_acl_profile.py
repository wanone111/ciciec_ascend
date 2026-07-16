#!/usr/bin/env python3
"""Summarize ACL profiling fields from pipeline.jsonl."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from statistics import mean


DEFAULT_ASCEND_LOG_DIR = Path("/root/userdata/ciciec/ascend_310B/log")
PREFIXES = ("pose", "face", "expression")
METRICS = (
    "acl_input_pack_ms",
    "acl_h2d_ms",
    "acl_execute_ms",
    "acl_d2h_ms",
    "acl_output_decode_ms",
    "acl_total_ms",
)


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    idx = int(round((len(ordered) - 1) * q))
    return ordered[max(0, min(idx, len(ordered) - 1))]


def summarize(values: list[float]) -> dict[str, float | int]:
    if not values:
        return {"count": 0, "avg": 0.0, "p50": 0.0, "p95": 0.0, "max": 0.0}
    return {
        "count": len(values),
        "avg": mean(values),
        "p50": percentile(values, 0.50),
        "p95": percentile(values, 0.95),
        "max": max(values),
    }


def load_profile(path: Path) -> dict[str, dict[str, list[float]]]:
    buckets: dict[str, dict[str, list[float]]] = {
        prefix: {metric: [] for metric in METRICS} for prefix in PREFIXES
    }
    with path.open("r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                item = json.loads(line)
            except json.JSONDecodeError:
                continue
            if item.get("kind") != "frame":
                continue
            for prefix in PREFIXES:
                for metric in METRICS:
                    value = item.get(f"{prefix}_{metric}")
                    if isinstance(value, (int, float)):
                        buckets[prefix][metric].append(float(value))
    return buckets


def print_table(summary: dict[str, dict[str, dict[str, float | int]]]) -> None:
    print("| model | metric | count | avg_ms | p50_ms | p95_ms | max_ms |")
    print("|---|---|---:|---:|---:|---:|---:|")
    for prefix in PREFIXES:
        for metric in METRICS:
            stats = summary[prefix][metric]
            if stats["count"] == 0:
                continue
            print(
                f"| {prefix} | {metric} | {stats['count']} | "
                f"{stats['avg']:.3f} | {stats['p50']:.3f} | "
                f"{stats['p95']:.3f} | {stats['max']:.3f} |"
            )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--pipeline-log",
        type=Path,
        default=DEFAULT_ASCEND_LOG_DIR / "pipeline.jsonl",
        help="Path to pipeline.jsonl.",
    )
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON.")
    args = parser.parse_args()

    if not args.pipeline_log.exists():
        raise SystemExit(f"pipeline log not found: {args.pipeline_log}")

    buckets = load_profile(args.pipeline_log)
    summary = {
        prefix: {metric: summarize(values) for metric, values in metrics.items()}
        for prefix, metrics in buckets.items()
    }

    if args.json:
        print(json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True))
    else:
        print_table(summary)


if __name__ == "__main__":
    main()
