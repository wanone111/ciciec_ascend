from __future__ import annotations

import unittest
from pathlib import Path
from types import SimpleNamespace

import numpy as np

from inference.expression_infer import ExpressionInfer
from inference.face_detector import FaceDetector
from inference.pose_infer import PoseInfer


class AippContractTests(unittest.TestCase):
    def test_theoretical_input_sizes(self):
        self.assertEqual(640 * 640 * 3, 1_228_800)
        self.assertEqual(640 * 640 * 3 // 2, 614_400)
        self.assertEqual(112 * 112 * 3, 37_632)
        self.assertEqual(112 * 112 * 3 // 2, 18_816)

    def test_pose_aipp_bgr_is_packed_without_rb_swap(self):
        image = np.zeros((640, 640, 3), dtype=np.uint8)
        image[0, 0] = (7, 11, 19)
        model = PoseInfer.__new__(PoseInfer)
        model.input_mode = "aipp_bgr_u8"
        model.model_path = "pose_aipp.om"
        model._om_session = SimpleNamespace(input_sizes=[image.nbytes], input_dims=[[1, 3, 640, 640]])
        out = model._prepare_om_input(image)
        self.assertEqual(tuple(out[0, 0]), (7, 11, 19))
        self.assertEqual(out.shape, (640, 640, 3))
        self.assertEqual(out.dtype, np.uint8)

    def test_expression_aipp_bgr_is_packed(self):
        roi = np.full((20, 30, 3), (3, 5, 9), dtype=np.uint8)
        model = ExpressionInfer.__new__(ExpressionInfer)
        model.input_mode = "aipp_bgr_u8"
        model.model_path = "expr_aipp.om"
        model.img_size = 112
        model._om_session = SimpleNamespace(input_sizes=[37_632], input_dims=[[1, 3, 112, 112]])
        out = model._preprocess_om(roi)
        self.assertEqual(out.shape, (112, 112, 3))
        self.assertEqual(tuple(out[0, 0]), (3, 5, 9))

    def test_face_default_remains_float_nchw_bgr_0_255(self):
        frame = np.full((20, 40, 3), (1, 2, 3), dtype=np.uint8)
        model = FaceDetector.__new__(FaceDetector)
        model.input_mode = "float_nchw"
        model.model_path = "face.om"
        model.input_size = (640, 640)
        model._om_session = SimpleNamespace(input_sizes=[4_915_200], input_dims=[[1, 3, 640, 640]])
        out, _ = model._preprocess_om(frame)
        self.assertEqual(out.shape, (1, 3, 640, 640))
        self.assertEqual(tuple(out[0, :, 0, 0]), (1.0, 2.0, 3.0))


if __name__ == "__main__":
    unittest.main()
