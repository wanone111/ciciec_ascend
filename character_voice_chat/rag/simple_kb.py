import os


def load_kb(folder_path):
    kb = []
    for filename in os.listdir(folder_path):
        path = os.path.join(folder_path, filename)
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8") as f:
                text = f.read()
                kb.append({
                    "filename": filename,
                    "text": text
                })
    return kb


def simple_search(query, kb, top_k=3):
    query = query.lower()
    scored = []

    for item in kb:
        text = item["text"].lower()

        score = 0
        for token in query:
            if token.strip() and token in text:
                score += 1

        # 文件名也参与一点权重
        if any(ch in item["filename"].lower() for ch in query if ch.strip()):
            score += 2

        scored.append((score, item["text"]))

    scored.sort(key=lambda x: x[0], reverse=True)
    return [text for score, text in scored[:top_k]]