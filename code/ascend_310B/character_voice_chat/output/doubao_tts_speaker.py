import io
import threading
import requests
import sounddevice as sd
import soundfile as sf

from .speaker import Speaker


class DoubaoTTSSpeaker(Speaker):
    API_URL = "https://openspeech.bytedance.com/api/v1/tts"

    def __init__(self, app_id, token, voice_type="zh_male_xionger_uranus_bigtts"):
        self.app_id = app_id
        self.token = token
        self.voice_type = voice_type
        self._play_lock = threading.Lock()
        self._state_lock = threading.Lock()
        self._speaking = False
        self._generation = 0

    def speak(self, message: dict):
        text = message.get("text", "")
        emotion = message.get("emotion", "neutral")
        if not text:
            return
        print(f"熊二[{emotion}]: {text}")
        # 新回复取代旧回复；旧合成请求即使稍后返回，也不允许再次播放。
        self.interrupt()
        with self._state_lock:
            self._generation += 1
            generation = self._generation
            self._speaking = True
        threading.Thread(
            target=self._synthesize_and_play,
            args=(text, generation),
            daemon=True,
        ).start()

    def _synthesize_and_play(self, text: str, generation: int):
        with self._play_lock:
            try:
                payload = {
                    "app": {"appid": self.app_id, "token": self.token, "cluster": "volcano_tts"},
                    "user": {"uid": "user"},
                    "audio": {
                        "voice_type": self.voice_type,
                        "encoding": "wav",
                        "sample_rate": 24000,
                    },
                    "request": {
                        "reqid": __import__("uuid").uuid4().hex,
                        "text": text,
                        "operation": "query",
                    },
                }
                resp = requests.post(
                    self.API_URL,
                    json=payload,
                    headers={"Authorization": f"Bearer;{self.token}"},
                    timeout=15,
                )
                resp.raise_for_status()
                data = resp.json()
                audio_b64 = data.get("data", "")
                if not audio_b64:
                    print("TTS错误: 返回数据为空", data)
                    return
                import base64
                audio_bytes = base64.b64decode(audio_b64)
                audio_buf = io.BytesIO(audio_bytes)
                samples, sr = sf.read(audio_buf, dtype="float32")
                if not self._is_current(generation):
                    return
                sd.play(samples, samplerate=sr)
                sd.wait()
            except Exception as e:
                if self._is_current(generation):
                    print("TTS错误:", e)
            finally:
                with self._state_lock:
                    if generation == self._generation:
                        self._speaking = False

    def _is_current(self, generation: int) -> bool:
        with self._state_lock:
            return generation == self._generation and self._speaking

    def interrupt(self):
        with self._state_lock:
            self._generation += 1
            self._speaking = False
        sd.stop()

    def is_speaking(self) -> bool:
        with self._state_lock:
            return self._speaking
