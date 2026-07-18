from __future__ import annotations

import queue
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np

from communication.udp_video_source import UDPVideoSource


class UdpRawNv12Tests(unittest.TestCase):
    def _source(self):
        source = UDPVideoSource.__new__(UDPVideoSource)
        source._frame_queue = queue.Queue(maxsize=2)
        source._read_timeout = 0.01
        source.last_frame_info = {}
        return source

    def test_raw_read_owns_copy_and_never_calls_cvtcolor(self):
        source = self._source()
        backing = np.arange(12, dtype=np.uint8)
        frame = SimpleNamespace(data=backing, width=4, height=2, pixel_format=3, frame_id=9, timestamp_us=10)
        source._on_frame(frame)
        backing.fill(0)
        with patch("communication.udp_video_source.cv2.cvtColor") as convert:
            ok, raw, info = source.read_raw_with_info()
        self.assertTrue(ok)
        self.assertTrue(info["raw_nv12_valid"])
        self.assertEqual(raw.frame_id, 9)
        self.assertNotEqual(int(raw.data[1]), 0)
        convert.assert_not_called()

    def test_rejects_odd_or_wrong_length_nv12(self):
        source = self._source()
        frame = SimpleNamespace(data=np.zeros(9, np.uint8), width=3, height=2, pixel_format=3, frame_id=1, timestamp_us=2)
        source._on_frame(frame)
        ok, raw, info = source.read_raw_with_info()
        self.assertFalse(ok)
        self.assertIsNone(raw)
        self.assertIn("invalid NV12", info["diagnostic"])


if __name__ == "__main__":
    unittest.main()
