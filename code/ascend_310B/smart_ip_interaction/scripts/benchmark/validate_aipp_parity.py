#!/usr/bin/env python3
"""Board A/B: legacy float OM versus static-AIPP packed-BGR OM."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))
from inference.acl_om import AclOmSession  # noqa: E402
from inference.dvpp import DvppProcessor  # noqa: E402
from behavior.action_rules import is_cheer_frame, is_heart_frame, is_wave_frame, valid_keypoints  # noqa: E402
from inference.pose_infer import PoseInfer, PoseModelConfig, _reshape_unknown_pose_output  # noqa: E402


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def samples(paths, size):
    colors = {
        "solid_red": (0, 0, 255), "solid_green": (0, 255, 0),
        "solid_blue": (255, 0, 0), "gray_114": (114, 114, 114),
    }
    for name, color in colors.items():
        yield name, np.full((size, size, 3), color, np.uint8)
    bars = np.zeros((size, size, 3), np.uint8)
    palette = [(255, 255, 255), (0, 255, 255), (255, 255, 0), (0, 255, 0), (255, 0, 255), (0, 0, 255), (255, 0, 0), (0, 0, 0)]
    for index, color in enumerate(palette):
        bars[:, index * size // len(palette):(index + 1) * size // len(palette)] = color
    yield "color_bars", bars
    horizontal = np.zeros((size, size, 3), np.uint8)
    for index, color in enumerate(palette):
        horizontal[index * size // len(palette):(index + 1) * size // len(palette), :] = color
    yield "horizontal_color_bars", horizontal
    yy, xx = np.indices((size, size))
    checker = np.zeros((size, size, 3), np.uint8)
    checker[((xx // 32 + yy // 32) & 1) == 0] = (0, 0, 255)
    checker[((xx // 32 + yy // 32) & 1) == 1] = (255, 0, 0)
    yield "checkerboard_red_blue", checker
    gradient = np.empty((size, size, 3), np.uint8)
    gradient[..., 0] = np.linspace(0, 255, size, dtype=np.uint8)[None, :]
    gradient[..., 1] = np.linspace(255, 0, size, dtype=np.uint8)[:, None]
    gradient[..., 2] = ((xx + yy) * 255 // max(1, 2 * size - 2)).astype(np.uint8)
    yield "rgb_gradient", gradient
    for raw in paths:
        path = Path(raw)
        image = cv2.imread(str(path))
        if image is None:
            raise SystemExit(f"cannot read image: {path}")
        yield path.name, image


def preprocess(task, image, size):
    if task == "pose":
        bgr, ratio_pad = PoseInfer._letterbox(image, size)
    else:
        bgr = cv2.resize(image, (size, size), interpolation=cv2.INTER_LINEAR)
        ratio_pad = None
    rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
    legacy = np.ascontiguousarray(rgb.transpose(2, 0, 1)[None], dtype=np.float32) / 255.0
    return legacy, np.ascontiguousarray(bgr), ratio_pad


def _bbox_iou(left, right):
    x1, y1 = np.maximum(left[:2], right[:2])
    x2, y2 = np.minimum(left[2:], right[2:])
    intersection = max(0.0, float(x2 - x1)) * max(0.0, float(y2 - y1))
    left_area = max(0.0, float(left[2] - left[0])) * max(0.0, float(left[3] - left[1]))
    right_area = max(0.0, float(right[2] - right[0])) * max(0.0, float(right[3] - right[1]))
    union = left_area + right_area - intersection
    return intersection / union if union > 0 else 0.0


def _action_flags(keypoints):
    if keypoints is None or not valid_keypoints(keypoints):
        return {"wave": False, "heart": False, "cheer": False}
    return {
        "wave": bool(is_wave_frame(keypoints)),
        "heart": bool(is_heart_frame(keypoints)),
        "cheer": bool(is_cheer_frame(keypoints)),
    }


def _pose_metrics(
    old_output, new_output, frame, old_ratio_pad, new_ratio_pad, size,
    visualization_path=None,
):
    decoder = object.__new__(PoseInfer)
    decoder.cfg = PoseModelConfig(imgsz=size)
    old_result = decoder._postprocess_output(
        _reshape_unknown_pose_output(old_output), frame, old_ratio_pad
    )
    new_result = decoder._postprocess_output(
        _reshape_unknown_pose_output(new_output), frame, new_ratio_pad
    )
    record = {
        "detections_baseline": len(old_result.boxes),
        "detections_candidate": len(new_result.boxes),
        "main_bbox_iou": None,
        "main_score_abs": None,
        "keypoint_xy_max_abs_px": None,
        "keypoint_xy_mean_abs_px": None,
        "action_baseline": _action_flags(None),
        "action_candidate": _action_flags(None),
        "action_match": True,
    }
    if len(old_result.boxes) and len(new_result.boxes):
        old_index = int(np.argmax(old_result.boxes.conf))
        new_index = int(np.argmax(new_result.boxes.conf))
        old_box = old_result.boxes.xyxy[old_index]
        new_box = new_result.boxes.xyxy[new_index]
        old_kp = old_result.keypoints.data[old_index]
        new_kp = new_result.keypoints.data[new_index]
        delta = np.abs(old_kp[:, :2] - new_kp[:, :2])
        record.update(
            main_bbox_iou=float(_bbox_iou(old_box, new_box)),
            main_score_abs=float(abs(old_result.boxes.conf[old_index] - new_result.boxes.conf[new_index])),
            keypoint_xy_max_abs_px=float(delta.max(initial=0)),
            keypoint_xy_mean_abs_px=float(delta.mean()),
            action_baseline=_action_flags(old_kp),
            action_candidate=_action_flags(new_kp),
        )
        record["action_match"] = record["action_baseline"] == record["action_candidate"]
    elif len(old_result.boxes) != len(new_result.boxes):
        record["action_match"] = False
    if visualization_path is not None:
        visualization_path.parent.mkdir(parents=True, exist_ok=True)
        left = old_result.plot()
        right = new_result.plot()
        cv2.putText(left, "float baseline", (12, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2)
        cv2.putText(right, "candidate", (12, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2)
        cv2.imwrite(str(visualization_path), np.concatenate((left, right), axis=1))
        record["visualization"] = str(visualization_path)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task", choices=("pose", "expression"), required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument(
        "--candidate-mode",
        choices=("float_nchw", "aipp_bgr_u8", "aipp_nv12"),
        default="aipp_bgr_u8",
    )
    parser.add_argument("--input", action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--visualize-dir", type=Path)
    parser.add_argument("--device-id", type=int, default=0)
    args = parser.parse_args()
    size = 640 if args.task == "pose" else 112
    baseline = AclOmSession(str(args.baseline), args.device_id, input_mode="float_nchw")
    candidate = AclOmSession(str(args.candidate), args.device_id, input_mode=args.candidate_mode)
    dvpp = DvppProcessor(baseline.shared_runtime, args.device_id) if args.candidate_mode == "aipp_nv12" else None
    records = []
    try:
        for name, image in samples(args.input, size):
            comparison_image = image
            if dvpp is not None and (image.shape[0] % 2 or image.shape[1] % 2):
                comparison_image = image[: image.shape[0] // 2 * 2, : image.shape[1] // 2 * 2]
            float_input, packed_bgr, old_ratio_pad = preprocess(args.task, comparison_image, size)
            old = baseline.execute(float_input)
            geometry_meta = None
            new_ratio_pad = old_ratio_pad
            if dvpp is None:
                new = candidate.execute(
                    float_input if args.candidate_mode == "float_nchw" else packed_bgr
                )
            else:
                source = dvpp.upload_bgr_or_rgb(comparison_image, key="parity_source")
                if args.task == "pose":
                    device_input, geometry = dvpp.make_pose_letterbox(source, size)
                    new_ratio_pad = ((geometry.ratio, geometry.ratio), geometry.pad)
                else:
                    cropped = dvpp.crop_expression(source, (0, 0, source.width, source.height), size)
                    if cropped is None:
                        raise RuntimeError(f"invalid full-frame expression ROI for {name}")
                    device_input, geometry = cropped
                geometry_meta = geometry.__dict__ if hasattr(geometry, "__dict__") else geometry
                new = candidate.execute_device_input(device_input.ptr, device_input.size)
            if len(old) != len(new):
                raise RuntimeError(f"output count differs for {name}: {len(old)} != {len(new)}")
            diffs = []
            for index, (left, right) in enumerate(zip(old, new)):
                if left.shape != right.shape:
                    raise RuntimeError(f"output {index} shape differs: {left.shape} != {right.shape}")
                delta = np.abs(left.astype(np.float64) - right.astype(np.float64))
                diffs.append({"output": index, "shape": list(left.shape), "max_abs": float(delta.max(initial=0)), "mean_abs": float(delta.mean())})
            item = {"sample": name, "outputs": diffs, "baseline_profile": baseline.last_profile, "candidate_profile": candidate.last_profile, "dvpp_profile": (dvpp.last_profile if dvpp else None), "dvpp_geometry": geometry_meta}
            if args.task == "expression":
                old_probs = np.exp(old[0].reshape(-1) - np.max(old[0]))
                old_probs /= old_probs.sum()
                new_probs = np.exp(new[0].reshape(-1) - np.max(new[0]))
                new_probs /= new_probs.sum()
                item.update(top1_baseline=int(old_probs.argmax()), top1_candidate=int(new_probs.argmax()), probability_max_abs=float(np.max(np.abs(old_probs - new_probs))))
            else:
                visualization_path = None
                if args.visualize_dir is not None and Path(name).suffix:
                    visualization_path = args.visualize_dir / f"{Path(name).stem}_pose_compare.jpg"
                item["pose"] = _pose_metrics(
                    old[0], new[0], comparison_image, old_ratio_pad, new_ratio_pad, size,
                    visualization_path,
                )
            records.append(item)
    finally:
        if dvpp is not None:
            dvpp.close()
        candidate.close()
        baseline.close()
    result = {
        "status": "measured_on_board", "task": args.task,
        "candidate_mode": args.candidate_mode,
        "baseline": {"path": str(args.baseline), "sha256": sha(args.baseline)},
        "candidate": {"path": str(args.candidate), "sha256": sha(args.candidate)},
        "samples": records,
        "acceptance_note": "AIPP is inside model execute; no separate AIPP time is claimed",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
