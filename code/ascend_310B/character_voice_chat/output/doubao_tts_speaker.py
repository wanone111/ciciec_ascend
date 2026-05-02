import io
import threading
import requests
import sounddevice as sd
import soundfile as sf

from ciciec_ascend.character_voice_chat.output.speaker import Speaker


class DoubaoTTSSpeaker(Speaker):
    API_URL = "https://openspeech.bytedance.com/api/v1/tts"

    def __init__(self, app_id, token, voice_type="zh_male_xionger_uranus_bigtts"):
        self.app_id = app_id
        self.token = token
        self.voice_type = voice_type
        self._lock = threading.Lock()
        self._speaking = False

    def speak(self, message: dict):
        text = message.get("text", "")
        emotion = message.get("emotion", "neutral")
        if not text:
            return
        print(f"熊二[{emotion}]: {text}")
        threading.Thread(target=self._synthesize_and_play, args=(text,), daemon=True).start()

    def _synthesize_and_play(self, text: str):
        with self._lock:
            self._speaking = True
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
                sd.play(samples, samplerate=sr)
                sd.wait()
            except Exception as e:
                print("TTS错误:", e)
            finally:
                self._speaking = False

    def interrupt(self):
        sd.stop()
        self._speaking = False

    def is_speaking(self) -> bool:
        return self._speaking
