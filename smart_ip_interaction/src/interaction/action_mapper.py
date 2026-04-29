# ===== 统一事件 → 指令映射 =====

def map_event_to_command(event):
    event_type = event.get("type")

    # ===== 动作 =====
    if event_type == "action":
        action = event.get("action")

        if action == "WAVE":
            return {"type": "action", "value": "wave_hand"}

        elif action == "HEART":
            return {"type": "action", "value": "heart_particles"}

        elif action == "CHEER":
            return {"type": "action", "value": "cheer_light_sound"}

    # ===== 表情（6分类）=====
    elif event_type == "expression":
        expression = event.get("expression")

        if expression == "happy":
            return {"type": "expression", "value": "smile_feedback"}

        elif expression == "surprised":
            return {"type": "expression", "value": "wow_feedback"}

        elif expression == "neutral":
            return {"type": "expression", "value": "idle_feedback"}

        elif expression == "angry":
            return {"type": "expression", "value": "angry_feedback"}

        elif expression == "fear":
            return {"type": "expression", "value": "fear_feedback"}

        elif expression == "sad":
            return {"type": "expression", "value": "sad_feedback"}

    return None
