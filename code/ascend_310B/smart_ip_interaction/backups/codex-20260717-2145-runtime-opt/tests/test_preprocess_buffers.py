from __future__ import annotations

import unittest
from types import SimpleNamespace

import numpy as np

from inference.face_detector import FaceDetector
from inference.pose_infer import PoseInfer


def _session(shape):
    return SimpleNamespace(
        input_dims=[list(shape)],
        input_sizes=[int(np.prod(shape)) * np.dtype(np.float32).itemsize],
    )


class ReusablePreprocessBufferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rng = np.random.default_rng(7)
        cls.frame = rng.integers(0, 256, (720, 1280, 3), dtype=np.uint8)
        cls.square = rng.integers(0, 256, (640, 640, 3), dtype=np.uint8)

    def test_pose_buffer_matches_legacy_and_is_reused(self):
        model = PoseInfer.__new__(PoseInfer)
        model._om_session = _session((1, 3, 640, 640))
        model._om_input_buffer = None
        model.reuse_input_buffer = False
        legacy = model._prepare_om_input(self.square)

        model.reuse_input_buffer = True
        first = model._prepare_om_input(self.square)
        second = model._prepare_om_input(self.square)

        np.testing.assert_array_equal(first, legacy)
        self.assertIs(first, second)

    def test_face_buffers_match_legacy_and_are_reused(self):
        model = FaceDetector.__new__(FaceDetector)
        model._om_session = _session((1, 3, 640, 640))
        model.input_size = (640, 640)
        model._input_canvas = None
        model._om_input_buffer = None
        model.reuse_input_buffer = False
        legacy, legacy_scale = model._preprocess_om(self.frame)

        model.reuse_input_buffer = True
        first, first_scale = model._preprocess_om(self.frame)
        canvas = model._input_canvas
        second, second_scale = model._preprocess_om(self.frame)

        np.testing.assert_array_equal(first, legacy)
        self.assertEqual((first_scale, second_scale), (legacy_scale, legacy_scale))
        self.assertIs(first, second)
        self.assertIs(canvas, model._input_canvas)


if __name__ == "__main__":
    unittest.main()
