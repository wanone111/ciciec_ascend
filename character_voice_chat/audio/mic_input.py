import numpy as np
import sounddevice as sd
from scipy.io.wavfile import write


class MicrophoneInput:
    def __init__(self, device=None, samplerate=16000, channels=1, dtype="float32"):
        self.device = device
        self.samplerate = samplerate
        self.channels = channels
        self.dtype = dtype

    def record_until_silence(
        self,
        speech_threshold=0.01,
        silence_threshold=0.008,
        silence_duration=1.8,
        max_duration=30.0,
        chunk_ms=100,
    ):
        """按下回车后开始监听，检测到说话自动录音，停止说话后自动结束。"""
        chunk_size = int(self.samplerate * chunk_ms / 1000)
        max_chunks = int(max_duration * 1000 / chunk_ms)
        silence_chunks_needed = int(silence_duration * 1000 / chunk_ms)

        frames = []
        speech_started = False
        silence_count = 0

        print("🎤 请说话...")
        with sd.InputStream(
            device=self.device,
            samplerate=self.samplerate,
            channels=self.channels,
            dtype=self.dtype,
            blocksize=chunk_size,
        ) as stream:
            for _ in range(max_chunks):
                chunk, _ = stream.read(chunk_size)
                chunk = chunk.flatten()
                amplitude = float(np.max(np.abs(chunk)))

                if not speech_started:
                    if amplitude >= speech_threshold:
                        speech_started = True
                        print("🔴 录音中...")
                        frames.append(chunk)
                else:
                    frames.append(chunk)
                    if amplitude < silence_threshold:
                        silence_count += 1
                        if silence_count >= silence_chunks_needed:
                            break
                    else:
                        silence_count = 0

        if not frames:
            return np.array([], dtype=np.float32)
        print("⏹ 录音结束")
        return np.concatenate(frames).astype(np.float32)

    def record_seconds(self, duration=4, save_path=None):
        print(f"[mic] 准备录音 device={self.device}, sr={self.samplerate}, ch={self.channels}")
        print("[mic] 调用 sd.rec ...")
        audio = sd.rec(
            int(duration * self.samplerate),
            samplerate=self.samplerate,
            channels=self.channels,
            dtype=self.dtype,
            device=self.device,
        )
        print("[mic] sd.rec 已返回，等待 sd.wait ...")
        sd.wait()
        print("[mic] sd.wait 结束，录音完成")

        audio = audio.flatten().astype(np.float32)

        if save_path:
            audio_int16 = (np.clip(audio, -1, 1) * 32767).astype(np.int16)
            write(save_path, self.samplerate, audio_int16)
            print(f"[mic] 已保存录音到: {save_path}")

        print(f"[mic] 最大振幅: {float(np.max(np.abs(audio)))}")
        return audio