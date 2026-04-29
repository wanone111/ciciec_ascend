class Speaker:
    def speak(self, message: dict):
        raise NotImplementedError

    def interrupt(self):
        raise NotImplementedError

    def is_speaking(self) -> bool:
        raise NotImplementedError