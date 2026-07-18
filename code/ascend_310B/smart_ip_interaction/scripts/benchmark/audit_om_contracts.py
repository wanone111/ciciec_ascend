#!/usr/bin/env python3
"""Audit real OM input bytes/dims; configured modes remain the source of truth."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "src"))

from inference.acl_om import AclOmSession, prod  # noqa: E402


THEORY = {
    "aipp_bgr_u8:640": 640 * 640 * 3,
    "aipp_nv12:640": 640 * 640 * 3 // 2,
    "aipp_bgr_u8:112": 112 * 112 * 3,
    "aipp_nv12:112": 112 * 112 * 3 // 2,
    "float_nchw:640": 1 * 3 * 640 * 640 * 4,
    "float_nchw:112": 1 * 3 * 112 * 112 * 4,
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validation_inference(dims, size):
    count = prod(dims)
    dtype = "uint8" if count and size == count else "float32" if count and size == count * 4 else "opaque_or_padded"
    layout = "NHWC" if dims and len(dims) == 4 and dims[-1] in (1, 3) else "NCHW_or_AIPP"
    return {"dtype_validation_only": dtype, "layout_validation_only": layout}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("models", nargs="+", type=Path)
    parser.add_argument("--mode", action="append", default=[], help="explicit mode per model, in positional order")
    parser.add_argument("--logical-size", action="append", type=int, default=[], help="640 or 112 per model")
    parser.add_argument("--device-id", type=int, default=0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.mode and len(args.mode) != len(args.models):
        parser.error("--mode count must match model count")
    if args.logical_size and len(args.logical_size) != len(args.models):
        parser.error("--logical-size count must match model count")

    sessions, records = [], []
    try:
        for index, raw_path in enumerate(args.models):
            path = raw_path.resolve()
            if not path.is_file():
                raise SystemExit(f"model not found: {path}")
            mode = args.mode[index] if args.mode else "unspecified"
            session = AclOmSession(str(path), args.device_id, input_mode=mode)
            sessions.append(session)
            input_records = []
            for name, dims, size in zip(session.input_names, session.input_dims, session.input_sizes):
                item = {"name": name, "dims": dims, "bytes": size, **validation_inference(dims, size)}
                if args.logical_size and mode != "unspecified":
                    key = f"{mode}:{args.logical_size[index]}"
                    expected = THEORY.get(key)
                    item.update(theoretical_tight_bytes=expected, tight_bytes_match=(expected == size if expected else None))
                input_records.append(item)
            records.append({
                "model": str(path), "sha256": sha256(path), "configured_mode": mode,
                "device_id": args.device_id, "shared_context": True,
                "runtime_refcount": session.runtime_refcount, "inputs": input_records,
                "outputs": [
                    {"name": name, "dims": dims, "bytes": size}
                    for name, dims, size in zip(session.output_names, session.output_dims, session.output_sizes)
                ],
            })
        result = {"models": records, "note": "dtype/layout inference is validation only; explicit mode is authoritative"}
        rendered = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True)
        print(rendered)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered + "\n", encoding="utf-8")
    finally:
        for session in reversed(sessions):
            session.close()


if __name__ == "__main__":
    main()
