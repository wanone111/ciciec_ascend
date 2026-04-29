import math
from collections import deque

# COCO 17 keypoints
NOSE = 0
LEFT_EYE = 1
RIGHT_EYE = 2
LEFT_EAR = 3
RIGHT_EAR = 4
LEFT_SHOULDER = 5
RIGHT_SHOULDER = 6
LEFT_ELBOW = 7
RIGHT_ELBOW = 8
LEFT_WRIST = 9
RIGHT_WRIST = 10
LEFT_HIP = 11
RIGHT_HIP = 12
LEFT_KNEE = 13
RIGHT_KNEE = 14
LEFT_ANKLE = 15
RIGHT_ANKLE = 16


def point_dist(p1, p2):
    return math.sqrt((float(p1[0]) - float(p2[0])) ** 2 + (float(p1[1]) - float(p2[1])) ** 2)


def point_mid(p1, p2):
    return ((float(p1[0]) + float(p2[0])) / 2.0, (float(p1[1]) + float(p2[1])) / 2.0)


def valid_keypoints(kp):
    if kp is None:
        return False
    if len(kp) < 17:
        return False
    return True


def upper_body_scale(kp):
    """
    用肩宽作为一个相对尺度，避免不同距离的人阈值不一致
    """
    ls = kp[LEFT_SHOULDER]
    rs = kp[RIGHT_SHOULDER]
    shoulder_dist = point_dist(ls, rs)
    return max(shoulder_dist, 1.0)


def is_wave_frame(kp, wrist_raise_ratio=0.10):
    """
    单帧挥手粗判断：
    任一手腕高于肩膀一定幅度
    """
    if not valid_keypoints(kp):
        return False

    ls = kp[LEFT_SHOULDER]
    rs = kp[RIGHT_SHOULDER]
    lw = kp[LEFT_WRIST]
    rw = kp[RIGHT_WRIST]

    shoulder_y = (float(ls[1]) + float(rs[1])) / 2.0
    scale = upper_body_scale(kp)

    left_up = float(lw[1]) < shoulder_y - wrist_raise_ratio * scale
    right_up = float(rw[1]) < shoulder_y - wrist_raise_ratio * scale

    return left_up or right_up


def is_cheer_frame(kp, wrist_raise_ratio=0.15):
    """
    单帧欢呼判断：
    双手同时高于头部/鼻子附近
    """
    if not valid_keypoints(kp):
        return False

    nose = kp[NOSE]
    lw = kp[LEFT_WRIST]
    rw = kp[RIGHT_WRIST]
    scale = upper_body_scale(kp)

    left_up = float(lw[1]) < float(nose[1]) - wrist_raise_ratio * scale
    right_up = float(rw[1]) < float(nose[1]) - wrist_raise_ratio * scale

    return left_up and right_up


def is_heart_frame(kp, wrist_close_ratio=0.9, chest_zone_ratio=0.9):
    """
    更宽松的单帧比心判断：
    1. 双手腕相对靠近
    2. 双手位于胸前区域
    3. 双手中点不要太偏离身体中心
    """
    if not valid_keypoints(kp):
        return False

    lw = kp[LEFT_WRIST]
    rw = kp[RIGHT_WRIST]
    ls = kp[LEFT_SHOULDER]
    rs = kp[RIGHT_SHOULDER]

    shoulder_dist = point_dist(ls, rs)
    if shoulder_dist < 1.0:
        return False

    wrist_dist = point_dist(lw, rw)

    chest_center = point_mid(ls, rs)
    wrist_mid = point_mid(lw, rw)

    wrists_close = wrist_dist < shoulder_dist * wrist_close_ratio
    wrists_in_chest_zone = abs(wrist_mid[1] - chest_center[1]) < shoulder_dist * chest_zone_ratio
    wrists_near_body_center = abs(wrist_mid[0] - chest_center[0]) < shoulder_dist * 1.0

    return wrists_close and wrists_in_chest_zone and wrists_near_body_center


class TemporalActionSmoother:
    """
    连续帧确认，减少抖动和误触发
    """
    def __init__(self, max_frames=5, trigger_frames=3):
        self.max_frames = max_frames
        self.trigger_frames = trigger_frames
        self.wave_buffer = deque(maxlen=max_frames)
        self.cheer_buffer = deque(maxlen=max_frames)
        self.heart_buffer = deque(maxlen=max_frames)

    def reset(self):
        self.wave_buffer.clear()
        self.cheer_buffer.clear()
        self.heart_buffer.clear()

    def update(self, kp):
        wave_now = is_wave_frame(kp)
        cheer_now = is_cheer_frame(kp)
        heart_now = is_heart_frame(kp)

        self.wave_buffer.append(wave_now)
        self.cheer_buffer.append(cheer_now)
        self.heart_buffer.append(heart_now)

        wave_count = sum(self.wave_buffer)
        cheer_count = sum(self.cheer_buffer)
        heart_count = sum(self.heart_buffer)

        # 优先级：CHEER > HEART > WAVE
        if cheer_count >= self.trigger_frames:
            return "CHEER"
        if heart_count >= self.trigger_frames:
            return "HEART"
        if wave_count >= self.trigger_frames:
            return "WAVE"
        return "NONE"

def debug_heart_frame(kp):
    if not valid_keypoints(kp):
        return {
            "valid": False
        }

    lw = kp[LEFT_WRIST]
    rw = kp[RIGHT_WRIST]
    le = kp[LEFT_ELBOW]
    re = kp[RIGHT_ELBOW]
    ls = kp[LEFT_SHOULDER]
    rs = kp[RIGHT_SHOULDER]

    shoulder_dist = point_dist(ls, rs)
    wrist_dist = point_dist(lw, rw)
    elbow_dist = point_dist(le, re)

    chest_center = point_mid(ls, rs)
    wrist_mid = point_mid(lw, rw)

    wrists_close = wrist_dist < shoulder_dist * 0.9
    wrists_in_chest_zone = abs(wrist_mid[1] - chest_center[1]) < shoulder_dist * 0.9
    wrists_near_body_center = abs(wrist_mid[0] - chest_center[0]) < shoulder_dist * 0.8
    elbows_inward = elbow_dist < shoulder_dist * 1.6

    return {
        "valid": True,
        "shoulder_dist": shoulder_dist,
        "wrist_dist": wrist_dist,
        "elbow_dist": elbow_dist,
        "wrist_mid": wrist_mid,
        "chest_center": chest_center,
        "wrists_close": wrists_close,
        "wrists_in_chest_zone": wrists_in_chest_zone,
        "wrists_near_body_center": wrists_near_body_center,
        "elbows_inward": elbows_inward,
    }