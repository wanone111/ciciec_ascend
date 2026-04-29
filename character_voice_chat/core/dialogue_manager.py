from ciciec_ascend.character_voice_chat.llm.chat_model import chat
from ciciec_ascend.character_voice_chat.llm.prompts import build_prompt
from ciciec_ascend.character_voice_chat.rag.simple_kb import simple_search
from ciciec_ascend.character_voice_chat.core.router import classify_question
from ciciec_ascend.character_voice_chat.core.reply_guard import out_of_role_reply


class DialogueManager:
    def __init__(self, kb):
        self.kb = kb
        self.history = []

    def handle_text(self, user_text: str) -> dict:
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
                reply = chat(prompt)
            except Exception as e:
                reply = f"俺也去这边出了点问题：{e}"

        self.history.append(("user", user_text))
        self.history.append(("assistant", reply))

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