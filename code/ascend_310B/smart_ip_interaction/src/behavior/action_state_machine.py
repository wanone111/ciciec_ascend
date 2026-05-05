import os
import time


def _env_int(name, default):
    raw = os.environ.get(name)
    if raw is None:
        return default
    try:
        return int(raw)
    except ValueError:
        return default


def _env_float(name, default):
    raw = os.environ.get(name)
    if raw is None:
        return default
    try:
        return float(raw)
    except ValueError:
        return default


class ActionStateMachine:
    def __init__(self, trigger_threshold=None, cooldown_time=None, rearm_frames=None):
        self.current_action = "NONE"
        self.last_trigger_time = 0

        self.wave_count = 0
        self.heart_count = 0
        self.cheer_count = 0

        # 连续确认 + 冷却 + 松手再触发，降低现场抖动和持续动作重复触发。
        self.trigger_threshold = (
            _env_int("SMART_IP_ACTION_TRIGGER_FRAMES", 4)
            if trigger_threshold is None else trigger_threshold
        )
        self.cooldown_time = (
            _env_float("SMART_IP_ACTION_COOLDOWN", 2.5)
            if cooldown_time is None else cooldown_time
        )
        self.rearm_frames = (
            _env_int("SMART_IP_ACTION_REARM_FRAMES", 3)
            if rearm_frames is None else rearm_frames
        )
        self.armed = True
        self.release_count = 0

    def update(self, wave, heart, cheer):
        now = time.time()
        has_action = wave or heart or cheer

        if has_action:
            self.release_count = 0
        else:
            self.release_count += 1
            if self.release_count >= self.rearm_frames:
                self.armed = True

        if not self.armed:
            return "NONE"

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
        self.armed = False
        self.release_count = 0

        self.wave_count = 0
        self.heart_count = 0
        self.cheer_count = 0
