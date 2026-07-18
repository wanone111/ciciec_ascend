from __future__ import annotations

import unittest

import numpy as np

from inference.dvpp import (
    _bgr_to_nv12_fullrange_reference, align_height, align_width, align_yuv420_roi, bgr_to_nv12_fullrange,
    face_canvas_geometry, make_nv12_canvas, pose_letterbox_geometry,
)
from inference.pose_infer import PoseInfer


class DvppGeometryTests(unittest.TestCase):
    def test_pose_geometry_matches_cpu_letterbox(self):
        for width, height in ((1920, 1080), (1280, 720), (720, 1280), (640, 640), (642, 478)):
            image = np.zeros((height, width, 3), dtype=np.uint8)
            cpu, ratio_pad = PoseInfer._letterbox(image, 640)
            geometry = pose_letterbox_geometry(width, height, 640)
            self.assertEqual(cpu.shape[:2], (640, 640))
            self.assertAlmostEqual(geometry.ratio, ratio_pad[0][0])
            self.assertEqual(geometry.pad, ratio_pad[1])
            self.assertEqual(sum(geometry.borders[::2]) + geometry.resized[0], 640)
            self.assertEqual(sum(geometry.borders[1::2]) + geometry.resized[1], 640)

    def test_face_is_top_left_not_centered(self):
        geometry = face_canvas_geometry(1920, 1080)
        self.assertEqual(geometry.paste_roi[:2], (0, 0))
        self.assertEqual(geometry.resized, (640, 360))

    def test_nv12_canvas_y_and_uv_differ(self):
        data = make_nv12_canvas(640, 640, 114, 128)
        self.assertTrue(np.all(data[: 640 * 640] == 114))
        self.assertTrue(np.all(data[640 * 640 :] == 128))

    def test_stride_and_roi_alignment(self):
        self.assertEqual(align_width(641), 656)
        self.assertEqual(align_height(479), 480)
        roi = align_yuv420_roi((3.2, 5.8, 101.1, 205.9), 640, 480)
        self.assertIsNotNone(roi)
        left, top, right, bottom = roi
        self.assertEqual((left % 2, top % 2, right % 2, bottom % 2), (0, 0, 1, 1))

    def test_fullrange_primary_colors(self):
        # For a 2x2 solid color, no chroma averaging ambiguity exists.
        red = np.full((2, 2, 3), (0, 0, 255), dtype=np.uint8)
        nv12 = bgr_to_nv12_fullrange(red)
        self.assertLessEqual(abs(int(nv12[0]) - 76), 1)
        self.assertLessEqual(abs(int(nv12[4]) - 85), 1)
        self.assertLessEqual(abs(int(nv12[5]) - 255), 1)

    def test_fast_fullrange_conversion_stays_within_one_lsb(self):
        frame = np.random.default_rng(7).integers(0, 256, (18, 20, 3), dtype=np.uint8)
        reference = _bgr_to_nv12_fullrange_reference(frame)
        fast = bgr_to_nv12_fullrange(frame)
        delta = np.abs(reference.astype(np.int16) - fast.astype(np.int16))
        self.assertLessEqual(int(delta.max()), 1)


if __name__ == "__main__":
    unittest.main()
