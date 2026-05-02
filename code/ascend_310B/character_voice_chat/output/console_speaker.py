from ciciec_ascend.character_voice_chat.output.speaker import Speaker


class ConsoleSpeaker(Speaker):
    def __init__(self):
        self._is_speaking = False
        self._last_message = None

    def speak(self, message: dict):
        self._is_speaking = True
        self._last_message = message

        text = message.get("text", "")
        emotion = message.get("emotion", "neutral")

        print(f"熊二[{emotion}]: {text}")

    def interrupt(self):
        if self._is_speaking:
            print(">>> 熊二输出被打断")
        self._is_speaking = False
        self._last_message = None

    def is_speaking(self) -> bool:
        return self._is_speaking

    def finish(self):
        self._is_speaking = False