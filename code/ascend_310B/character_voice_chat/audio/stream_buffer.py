import numpy as np


class AudioStreamBuffer:
    def __init__(self):
        self.chunks = []

    def add_chunk(self, chunk):
        self.chunks.append(chunk)

    def clear(self):
        self.chunks = []

    def has_data(self):
        return len(self.chunks) > 0

    def get_audio(self):
        if not self.chunks:
            return None
        return np.concatenate(self.chunks, axis=0)