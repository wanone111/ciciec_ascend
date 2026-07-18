from __future__ import annotations

import unittest
from types import SimpleNamespace

import numpy as np

from inference.expression_infer import ExpressionInfer


class ExpressionUint8InputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rng = np.random.default_rng(21)
        cls.roi = rng.integers(0, 256, (80, 140, 3), dtype=np.uint8)

    def _model(self, shape):
        model = ExpressionInfer.__new__(ExpressionInfer)
        model._om_session = SimpleNamespace(
            input_dims=[list(shape)], input_sizes=[int(np.prod(shape))]
        )
        model.img_size = 112
        model.fused_preprocess = True
        return model

    def test_nchw_uint8_contract(self):
        tensor = self._model((1, 3, 112, 112))._preprocess_om(self.roi)
        self.assertEqual(tensor.shape, (1, 3, 112, 112))
        self.assertEqual(tensor.dtype, np.uint8)
        self.assertTrue(tensor.flags.c_contiguous)

    def test_nhwc_uint8_contract(self):
        tensor = self._model((1, 112, 112, 3))._preprocess_om(self.roi)
        self.assertEqual(tensor.shape, (1, 112, 112, 3))
        self.assertEqual(tensor.dtype, np.uint8)
        self.assertTrue(tensor.flags.c_contiguous)


if __name__ == "__main__":
    unittest.main()


