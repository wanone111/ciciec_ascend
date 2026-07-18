from __future__ import annotations

import unittest
from types import SimpleNamespace

import numpy as np

from inference.acl_om import AclOmSession, NPY_FLOAT32


class _UtilWithView:
    def __init__(self, arrays):
        self.arrays = arrays
        self.calls = []

    def ptr_to_numpy(self, ptr, shape, dtype_code):
        self.calls.append((ptr, shape, dtype_code))
        return self.arrays[ptr].reshape(shape)


def _session(util, *, enabled=True):
    session = AclOmSession.__new__(AclOmSession)
    session.acl = SimpleNamespace(util=util)
    session.zero_copy_output_enabled = enabled
    session.output_host_ptrs = [101, 102]
    session.output_sizes = [24, 16]
    session.output_dims = [[1, 2, 3], [99]]
    session.output_host_arrays = []
    return session


class OutputHostViewTests(unittest.TestCase):
    def test_creates_reusable_views_with_model_shape_when_size_matches(self):
        arrays = {
            101: np.arange(6, dtype=np.float32),
            102: np.arange(4, dtype=np.float32),
        }
        util = _UtilWithView(arrays)
        session = _session(util)

        session._create_output_host_views()

        self.assertEqual(session.output_host_arrays[0].shape, (1, 2, 3))
        self.assertEqual(session.output_host_arrays[1].shape, (4,))
        self.assertFalse(session.output_host_arrays[0].flags.owndata)
        self.assertEqual(util.calls[0], (101, (1, 2, 3), NPY_FLOAT32))
        self.assertEqual(util.calls[1], (102, (4,), NPY_FLOAT32))

    def test_disabled_mode_uses_fallback_markers(self):
        session = _session(_UtilWithView({}), enabled=False)

        session._create_output_host_views()

        self.assertEqual(session.output_host_arrays, [None, None])

    def test_missing_api_uses_fallback_markers(self):
        session = _session(SimpleNamespace())

        session._create_output_host_views()

        self.assertEqual(session.output_host_arrays, [None, None])

    def test_rejects_non_float32_aligned_output(self):
        session = _session(_UtilWithView({}))

        with self.assertRaisesRegex(RuntimeError, "not float32-aligned"):
            session._output_shape(0, 7)


if __name__ == "__main__":
    unittest.main()


