from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from benchmark.logger import JsonlLogger


class AsyncLoggerTests(unittest.TestCase):
    def test_close_drains_pending_records(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pipeline.jsonl"
            logger = JsonlLogger(path, "pipeline")
            for index in range(100):
                logger.log("frame", frame_idx=index)
            logger.close()
            records = [json.loads(line) for line in path.read_text().splitlines()]
            self.assertEqual(len(records), 100)
            self.assertEqual(records[-1]["frame_idx"], 99)
            self.assertIn("monotonic_ns", records[-1])


if __name__ == "__main__":
    unittest.main()


