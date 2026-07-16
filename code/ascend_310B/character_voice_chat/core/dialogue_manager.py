from collections.abc import Callable

from ..llm.chat_model import chat
from ..llm.prompts import build_prompt
from ..rag.simple_kb import simple_search
from .router import classify_question
from .reply_guard import out_of_role_reply


class DialogueManager:
    def __init__(self, kb, chat_fn: Callable[[str], str] | None = None, max_history_turns=8):
        self.kb = kb
        self.history = []
        self.chat_fn = chat_fn or chat
        self.max_history_items = max(0, int(max_history_turns)) * 2

    def handle_text(self, user_text: str) -> dict:
        user_text = user_text.strip()
        if not user_text:
            return {"text": "", "emotion": "neutral", "action": "none"}

        qtype = classify_question(user_text)

        if qtype == "out_of_role":
            reply = out_of_role_reply(user_text)
        else:
            docs = simple_search(user_text, self.kb)

            prompt = build_prompt(
                user_text=user_text,
                retrieved_docs=docs,
                history=self.history
            )

            try:
                reply = self.chat_fn(prompt)
            except Exception as e:
                reply = f"俺也去这边出了点问题：{e}"

        self.history.append(("user", user_text))
        self.history.append(("assistant", reply))
        if self.max_history_items:
            self.history = self.history[-self.max_history_items:]
        else:
            self.history.clear()

        return {
            "text": reply,
            "emotion": self._infer_emotion(reply),
            "action": "talk"
        }

    def _infer_emotion(self, text: str) -> str:
        if any(k in text for k in ["哈哈", "嘿嘿"]):
            return "happy"
        if any(k in text for k in ["不懂", "不知道"]):
            return "confused"
        if any(k in text for k in ["别", "快点"]):
            return "impatient"
        return "neutral"
