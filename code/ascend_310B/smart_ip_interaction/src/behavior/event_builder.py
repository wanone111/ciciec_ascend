import time


# ===== 动作事件 =====
def build_action_event(action):
    return {
        "type": "action",
        "action": action,
        "timestamp": time.time(),
        "source": "pose_rule"
    }


# ===== 表情事件（你现在缺的就是这个）=====
def build_expression_event(expression, confidence):
    return {
        "type": "expression",
        "expression": expression,
        "confidence": confidence,
        "timestamp": time.time(),
        "source": "expression_model"
    }