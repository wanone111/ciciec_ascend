import numpy as np


class UtteranceDetector:
    def __init__(self, silence_threshold=0.003, silence_chunks_needed=5):
        self.silence_threshold = silence_threshold
        self.silence_chunks_needed = silence_chunks_needed
        self.silence_count = 0
        self.has_speech = False

    def update(self, chunk) -> bool:
        energy = float(np.mean(np.abs(chunk)))

        if energy > self.silence_threshold:
            self.has_speech = True
            self.silence_count = 0
            return False

        if self.has_speech:
            self.silence_count += 1
            if self.silence_count >= self.silence_chunks_needed:
                self.reset()
                return True

        return False

    def reset(self):
        self.silence_count = 0
        self.has_speech = False