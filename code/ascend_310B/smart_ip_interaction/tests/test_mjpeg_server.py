import threading
import unittest
from unittest.mock import patch

from streaming.mjpeg_server import MJPEGServer


class MJPEGServerTests(unittest.TestCase):
    def test_publish_rejects_none(self):
        server = MJPEGServer()

        self.assertFalse(server.publish(None))
        self.assertEqual(server.stats()["frames_submitted"], 0)

    def test_encoder_survives_condition_timeout(self):
        server = MJPEGServer()
        calls = 0

        def timed_out_wait_for(_predicate, timeout):
            nonlocal calls
            calls += 1
            self.assertEqual(timeout, 1.0)
            server._stop_event.set()
            return False

        with patch.object(
            server._pending_condition, "wait_for", timed_out_wait_for
        ):
            worker = threading.Thread(target=server._encode_loop)
            worker.start()
            worker.join(timeout=2.0)

        self.assertEqual(calls, 1)
        self.assertFalse(worker.is_alive())


if __name__ == "__main__":
    unittest.main()


