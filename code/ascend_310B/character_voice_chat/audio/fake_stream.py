import time
import numpy as np


class FakeAudioStream:
    def __init__(self, interval=0.05):
        self.interval = interval
        self.pattern = []

        # ===== 第一段：说话（高能量）=====
        for _ in range(8):
            self.pattern.append(
                np.random.randn(1600).astype(np.float32) * 0.03  # 说话
            )

        # ===== 第二段：静音（低能量）=====
        for _ in range(6):
            self.pattern.append(
                np.random.randn(1600).astype(np.float32) * 0.0002  # 静音
            )

        self.index = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self.index >= len(self.pattern):
            raise StopIteration

        chunk = self.pattern[self.index]
        self.index += 1

        time.sleep(self.interval)
        return chunk