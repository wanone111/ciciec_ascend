def classify_question(user_text: str) -> str:
    text = user_text.lower()

    reality_keywords = [
        "python", "编程", "人工智能", "ai", "大模型",
        "openai", "豆包", "网络", "代码", "api"
    ]

    role_keywords = [
        "熊大", "光头强", "森林", "蜂蜜", "你是谁",
        "你喜欢吃什么", "熊出没"
    ]

    for kw in reality_keywords:
        if kw in text:
            return "out_of_role"

    for kw in role_keywords:
        if kw in text:
            return "in_role"

    return "chat"