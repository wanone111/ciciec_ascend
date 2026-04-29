import time

class ActionStateMachine:
    def __init__(self):
        self.current_action = "NONE"
        self.last_trigger_time = 0

        self.wave_count = 0
        self.heart_count = 0
        self.cheer_count = 0

        # 放宽参数
        self.trigger_threshold = 2
        self.cooldown_time = 0.8

    def update(self, wave, heart, cheer):
        now = time.time()

        # 冷却期
        if now - self.last_trigger_time < self.cooldown_time:
            return "NONE"

        self.wave_count = self.wave_count + 1 if wave else 0
        self.heart_count = self.heart_count + 1 if heart else 0
        self.cheer_count = self.cheer_count + 1 if cheer else 0

        # 优先级：CHEER > HEART > WAVE
        if self.cheer_count >= self.trigger_threshold:
            self._trigger("CHEER", now)
            return "CHEER"

        if self.heart_count >= self.trigger_threshold:
            self._trigger("HEART", now)
            return "HEART"

        if self.wave_count >= self.trigger_threshold:
            self._trigger("WAVE", now)
            return "WAVE"

        return "NONE"

    def _trigger(self, action, now):
        self.current_action = action
        self.last_trigger_time = now

        self.wave_count = 0
        self.heart_count = 0
        self.cheer_count = 0