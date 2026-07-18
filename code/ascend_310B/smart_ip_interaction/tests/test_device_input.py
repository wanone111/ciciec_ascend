from __future__ import annotations

import unittest
from types import SimpleNamespace

from inference.acl_om import AclOmSession, ACL_MEMCPY_DEVICE_TO_DEVICE


class _Rt:
    def __init__(self): self.calls = []
    def memcpy(self, *args): self.calls.append(args); return 0


class DeviceInputTests(unittest.TestCase):
    def _session(self):
        rt = _Rt()
        acl = SimpleNamespace(
            rt=rt,
            mdl=SimpleNamespace(execute=lambda *args: 0),
            util=SimpleNamespace(ptr_to_bytes=lambda ptr, size: b""),
        )
        session = AclOmSession.__new__(AclOmSession)
        session.acl = acl
        session.model_path = "/models/pose_aipp_nv12.om"
        session.input_mode = "aipp_nv12"
        session.input_sizes = [614_400]
        session.input_dims = [[1, 3, 640, 640]]
        session.input_device_ptrs = [111]
        session.output_device_ptrs = []
        session.output_host_ptrs = []
        session.output_sizes = []
        session.output_dims = []
        session.model_id = 1
        session.input_dataset = 2
        session.output_dataset = 3
        session._closed = False
        session.context = None
        session.profile_enabled = True
        session.output_host_arrays = []
        session._runtime = SimpleNamespace(set_context=lambda: None, refcount=1)
        session.last_profile = {}
        return session, rt

    def test_size_mismatch_is_strict_and_diagnostic(self):
        session, _ = self._session()
        with self.assertRaisesRegex(RuntimeError, "expected_bytes=614400.*actual_bytes=10.*aipp_nv12"):
            session.execute_device_input(222, 10)

    def test_d2d_copy_kind_and_bytes(self):
        session, rt = self._session()
        session.execute_device_input(222, 614_400)
        self.assertEqual(rt.calls[0], (111, 614_400, 222, 614_400, ACL_MEMCPY_DEVICE_TO_DEVICE))
        self.assertEqual(session.last_profile["acl_d2d_bytes"], 614_400)

    def test_external_is_explicitly_blocked(self):
        session, _ = self._session()
        with self.assertRaises(NotImplementedError):
            session.execute_device_input(222, 614_400, copy_mode="external")


if __name__ == "__main__":
    unittest.main()
