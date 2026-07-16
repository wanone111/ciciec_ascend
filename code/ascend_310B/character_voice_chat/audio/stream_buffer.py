import numpy as np


class AudioStreamBuffer:
    def __init__(self, max_samples=None):
        self.chunks = []
        self.sample_count = 0
        self.max_samples = max_samples

    def add_chunk(self, chunk):
        chunk = np.asarray(chunk, dtype=np.float32).reshape(-1)
        if chunk.size == 0:
            return
        self.chunks.append(chunk)
        self.sample_count += chunk.size
        if self.max_samples and self.sample_count > self.max_samples:
            overflow = self.sample_count - self.max_samples
            while self.chunks and overflow >= self.chunks[0].size:
                removed = self.chunks.pop(0)
                overflow -= removed.size
                self.sample_count -= removed.size
            if overflow > 0 and self.chunks:
                self.chunks[0] = self.chunks[0][overflow:]
                self.sample_count -= overflow

    def clear(self):
        self.chunks.clear()
        self.sample_count = 0

    def has_data(self):
        return len(self.chunks) > 0

    def get_audio(self):
        if not self.chunks:
            return None
        return np.concatenate(self.chunks, axis=0)
