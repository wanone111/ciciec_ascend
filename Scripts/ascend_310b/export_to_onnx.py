#!/usr/bin/env python3
"""Export a static-shape Ultralytics pose checkpoint to validated ONNX."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil

import onnx
from ultralytics import YOLO


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True, help="Input Ultralytics .pt checkpoint")
    parser.add_argument("--output", type=Path, required=True, help="Destination .onnx path")
    parser.add_argument("--imgsz", type=int, default=640, help="Static square input size")
    parser.add_argument("--opset", type=int, default=11, help="ONNX opset for CANN compatibility")
    return parser.parse_args()


def tensor_shape(value_info) -> list[int | str | None]:
    dims = value_info.type.tensor_type.shape.dim
    return [dim.dim_value or dim.dim_param or None for dim in dims]


def main() -> None:
    args = parse_args()
    if args.model.suffix.lower() != ".pt" or not args.model.is_file():
        raise SystemExit(f"input checkpoint is missing or not .pt: {args.model}")
    if args.output.suffix.lower() != ".onnx":
        raise SystemExit(f"output must end with .onnx: {args.output}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    model = YOLO(str(args.model))
    exported = Path(
        model.export(
            format="onnx",
            imgsz=args.imgsz,
            opset=args.opset,
            dynamic=False,
            simplify=False,
            batch=1,
            device="cpu",
        )
    ).resolve()
    destination = args.output.resolve()
    if exported != destination:
        shutil.move(str(exported), str(destination))

    graph = onnx.load(str(destination))
    onnx.checker.check_model(graph)
    inputs = {item.name: tensor_shape(item) for item in graph.graph.input}
    outputs = {item.name: tensor_shape(item) for item in graph.graph.output}
    print(f"[OK] ONNX: {destination}")
    print(f"[OK] inputs: {inputs}")
    print(f"[OK] outputs: {outputs}")


if __name__ == "__main__":
    main()
