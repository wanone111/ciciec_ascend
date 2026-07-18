from __future__ import annotations

import unittest
from types import SimpleNamespace

import numpy as np

from inference.expression_infer import ExpressionInfer


def _session(shape):
    return SimpleNamespace(
        input_dims=[list(shape)],
        input_sizes=[int(np.prod(shape)) * np.dtype(np.float32).itemsize],
    )


class FusedPreprocessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rng = np.random.default_rng(42)
        cls.roi = rng.integers(0, 256, (333, 277, 3), dtype=np.uint8)

    def test_expression_fused_matches_legacy_with_float_roundoff_only(self):
        model = ExpressionInfer.__new__(ExpressionInfer)
        model._om_session = _session((1, 3, 112, 112))
        model.img_size = 112
        model.fused_preprocess = False
        legacy = model._preprocess_om(self.roi)
        model.fused_preprocess = True
        fused = model._preprocess_om(self.roi)

        self.assertEqual(fused.shape, legacy.shape)
        np.testing.assert_allclose(fused, legacy, rtol=0.0, atol=6e-8)


if __name__ == "__main__":
    unittest.main()


