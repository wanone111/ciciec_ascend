#!/usr/bin/env python3
"""Board validation of DVPP pose/face canvas values, stride and ROI geometry."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))
from inference.acl_om import AclOmSession  # noqa: E402
from inference.dvpp import DvppProcessor  # noqa: E402


def unpack(image, raw):
    y_size = image.width_stride * image.height_stride
    y = raw[:y_size].reshape(image.height_stride, image.width_stride)[:image.height, :image.width]
    uv = raw[y_size:].reshape(image.height_stride // 2, image.width_stride)[:image.height // 2, :image.width]
    return y, uv


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-model", type=Path, required=True, help="any audited OM used only to acquire shared ACL context")
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--device-id", type=int, default=0)
    args = parser.parse_args()
    frame = cv2.imread(str(args.image))
    if frame is None:
        raise SystemExit(f"cannot read {args.image}")
    if frame.shape[0] % 2 or frame.shape[1] % 2:
        frame = frame[: frame.shape[0] // 2 * 2, : frame.shape[1] // 2 * 2]
    session = AclOmSession(str(args.runtime_model), args.device_id)
    processor = DvppProcessor(session.shared_runtime, args.device_id)
    try:
        source = processor.upload_bgr_or_rgb(frame)
        pose, pose_geometry = processor.make_pose_letterbox(source)
        face, face_geometry = processor.make_face_canvas(source)
        expression_result = processor.crop_expression(
            source,
            (frame.shape[1] * 0.25, frame.shape[0] * 0.2,
             frame.shape[1] * 0.75, frame.shape[0] * 0.8),
        )
        if expression_result is None:
            raise RuntimeError("generated expression ROI was invalid")
        expression, expression_geometry = expression_result
        pose_y, pose_uv = unpack(pose, processor.download_nv12(pose))
        face_y, face_uv = unpack(face, processor.download_nv12(face))
        left, top, right, bottom = pose_geometry.borders
        pad_masks = []
        if top: pad_masks.append(pose_y[:top])
        if bottom: pad_masks.append(pose_y[-bottom:])
        if left: pad_masks.append(pose_y[:, :left])
        if right: pad_masks.append(pose_y[:, -right:])
        result = {
            "status": "measured_on_board", "source_size": [frame.shape[1], frame.shape[0]],
            "pose": {"geometry": pose_geometry.__dict__, "stride": [pose.width_stride, pose.height_stride], "y_padding_is_114": all(np.all(x == 114) for x in pad_masks), "uv_range": [int(pose_uv.min()), int(pose_uv.max())]},
            "face": {"geometry": face_geometry.__dict__, "stride": [face.width_stride, face.height_stride], "bottom_right_y": int(face_y[-1, -1]), "bottom_right_uv": [int(v) for v in face_uv[-1, -2:]]},
            "expression": {"geometry": expression_geometry, "stride": [expression.width_stride, expression.height_stride], "bytes": expression.size},
            "dvpp_profile": processor.last_profile,
        }
    finally:
        processor.close()
        session.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, default=list) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2, default=list))


if __name__ == "__main__":
    main()
