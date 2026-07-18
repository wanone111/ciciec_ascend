#!/usr/bin/env python3
"""Aggregate msprof op_summary CSV into compact operator evidence."""

from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path


def newest(root: Path, pattern: str) -> Path:
    matches = list(root.rglob(pattern))
    if not matches:
        raise SystemExit(f"no {pattern} under {root}")
    return max(matches, key=lambda path: path.stat().st_mtime_ns)


def as_float(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile_dir", type=Path)
    parser.add_argument("--top", type=int, default=15)
    parser.add_argument("--json-output", type=Path)
    parser.add_argument("--markdown-output", type=Path)
    args = parser.parse_args()

    csv_path = newest(args.profile_dir, "op_summary*.csv")
    with csv_path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise SystemExit(f"empty op summary: {csv_path}")

    infer_ids = {row.get("Infer ID", "") for row in rows}
    by_name = defaultdict(lambda: {"duration_us": 0.0, "calls": 0, "op_type": "", "task_type": ""})
    by_type = defaultdict(lambda: {"duration_us": 0.0, "calls": 0})
    by_task = defaultdict(lambda: {"duration_us": 0.0, "calls": 0})
    for row in rows:
        duration = as_float(row.get("Task Duration(us)"))
        name = row.get("Op Name", "unknown")
        op_type = row.get("OP Type", "unknown")
        task_type = row.get("Task Type", "unknown")
        item = by_name[name]
        item["duration_us"] += duration
        item["calls"] += 1
        item["op_type"] = op_type
        item["task_type"] = task_type
        by_type[op_type]["duration_us"] += duration
        by_type[op_type]["calls"] += 1
        by_task[task_type]["duration_us"] += duration
        by_task[task_type]["calls"] += 1

    divisor = max(1, len(infer_ids))
    total_us = sum(item["duration_us"] for item in by_name.values())
    top_ops = []
    for name, item in sorted(by_name.items(), key=lambda pair: pair[1]["duration_us"], reverse=True)[: args.top]:
        top_ops.append({
            "name": name,
            "op_type": item["op_type"],
            "task_type": item["task_type"],
            "calls": item["calls"],
            "mean_per_inference_us": item["duration_us"] / divisor,
            "share_percent": 100.0 * item["duration_us"] / total_us if total_us else 0.0,
        })

    result = {
        "source": str(csv_path),
        "model": rows[0].get("Model Name"),
        "inferences": len(infer_ids),
        "rows": len(rows),
        "mean_device_task_total_ms": total_us / divisor / 1000.0,
        "task_types": dict(by_task),
        "operator_types": dict(sorted(by_type.items(), key=lambda pair: pair[1]["duration_us"], reverse=True)),
        "aicpu": {
            "calls": sum(v["calls"] for k, v in by_task.items() if "AICPU" in k.upper()),
            "duration_us": sum(v["duration_us"] for k, v in by_task.items() if "AICPU" in k.upper()),
        },
        "top_ops": top_ops,
    }
    rendered = json.dumps(result, indent=2, ensure_ascii=False)
    print(rendered)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(rendered + "\n", encoding="utf-8")
    if args.markdown_output:
        lines = [
            f"# msprof summary: {result['model']}",
            "",
            f"- Inferences: {result['inferences']}",
            f"- Mean device task total: {result['mean_device_task_total_ms']:.3f} ms",
            f"- AICPU calls: {result['aicpu']['calls']}",
            "",
            "| op | type | task | calls | mean us/inference | share |",
            "|---|---|---|---:|---:|---:|",
        ]
        for op in top_ops:
            lines.append(
                f"| {op['name']} | {op['op_type']} | {op['task_type']} | "
                f"{op['calls']} | {op['mean_per_inference_us']:.3f} | "
                f"{op['share_percent']:.2f}% |"
            )
        args.markdown_output.parent.mkdir(parents=True, exist_ok=True)
        args.markdown_output.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
