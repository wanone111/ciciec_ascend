#!/usr/bin/env python3
"""Deterministic single-OM microbenchmark for ACL and msprof collection."""

from __future__ import annotations

import argparse
import json
import os
import statistics
import sys
import time
from pathlib import Path

import numpy as np


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "src"))

from inference.acl_om import AclOmSession, prod  # noqa: E402


def percentile(values, q):
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * q)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--device-id", type=int, default=0)
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--runs", type=int, default=50)
    parser.add_argument("--seed", type=int, default=20260717)
    parser.add_argument("--json-output", type=Path)
    args = parser.parse_args()

    os.environ["SMART_IP_ACL_PROFILE"] = "1"
    session = AclOmSession(str(args.model.resolve()), args.device_id)
    try:
        dims = session.input_dims[0]
        count = prod(dims)
        size = session.input_sizes[0]
        if size == count * 4:
            dtype = np.float32
        elif size == count:
            dtype = np.uint8
        else:
            raise SystemExit(f"unsupported input contract dims={dims} bytes={size}")
        rng = np.random.default_rng(args.seed)
        if dtype is np.float32:
            tensor = rng.random(tuple(dims), dtype=np.float32)
        else:
            tensor = rng.integers(0, 256, tuple(dims), dtype=np.uint8)

        for _ in range(max(0, args.warmup)):
            session.execute(tensor)
        samples = []
        profiles = []
        for _ in range(max(1, args.runs)):
            started = time.perf_counter()
            session.execute(tensor)
            samples.append((time.perf_counter() - started) * 1000.0)
            profiles.append(dict(session.last_profile))
        result = {
            "model": str(args.model.resolve()),
            "device_id": args.device_id,
            "input_dims": dims,
            "input_dtype": np.dtype(dtype).name,
            "warmup": args.warmup,
            "runs": args.runs,
            "execute_wall_ms": {
                "mean": statistics.mean(samples),
                "p50": percentile(samples, 0.50),
                "p95": percentile(samples, 0.95),
                "max": max(samples),
            },
            "acl_profile_mean_ms": {
                key: statistics.mean([p[key] for p in profiles])
                for key in profiles[0]
            },
        }
        rendered = json.dumps(result, indent=2, sort_keys=True)
        print(rendered)
        if args.json_output:
            args.json_output.parent.mkdir(parents=True, exist_ok=True)
            args.json_output.write_text(rendered + "\n", encoding="utf-8")
    finally:
        session.close()


if __name__ == "__main__":
    main()
