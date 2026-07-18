from __future__ import annotations

import unittest
from types import SimpleNamespace

from inference.dvpp import DvppProcessor


class _Media:
    PIXEL_FORMAT_YUV_SEMIPLANAR_420 = 1

    def __init__(self):
        self.calls = []
        self.next_ptr = 1000

    def __getattr__(self, name):
        if name.startswith("dvpp_create_"):
            return lambda *args: {"kind": name, "args": args}
        if name.startswith("dvpp_destroy_"):
            return lambda *args: self.calls.append((name, args)) or 0
        raise AttributeError(name)

    def dvpp_create_channel(self, desc): self.calls.append(("create_channel", desc)); return 0
    def dvpp_vpc_crop_and_paste_async(self, *args): self.calls.append(("vpc", args)); return 0
    def dvpp_malloc(self, size): self.next_ptr += 1; self.calls.append(("malloc", size)); return self.next_ptr, 0
    def dvpp_free(self, ptr): self.calls.append(("free", ptr)); return 0


class _Rt:
    def __init__(self): self.calls = []
    def create_stream(self): self.calls.append("create_stream"); return 77, 0
    def destroy_stream(self, stream): self.calls.append(("destroy_stream", stream)); return 0


class DvppLifecycleTests(unittest.TestCase):
    def test_reuses_buffer_and_close_is_idempotent(self):
        media, rt = _Media(), _Rt()
        runtime = SimpleNamespace(acl=SimpleNamespace(media=media, rt=rt))
        runtime.set_context = lambda: None
        processor = DvppProcessor(runtime)
        first = processor._get_buffer("pose", 640, 640, 1)
        second = processor._get_buffer("pose", 640, 640, 1)
        self.assertIs(first, second)
        self.assertEqual(sum(call[0] == "malloc" for call in media.calls), 1)
        processor.close()
        processor.close()
        self.assertEqual(sum(call[0] == "free" for call in media.calls), 1)
        self.assertEqual(rt.calls.count(("destroy_stream", 77)), 1)


if __name__ == "__main__":
    unittest.main()
