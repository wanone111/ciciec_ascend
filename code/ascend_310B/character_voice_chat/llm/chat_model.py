import requests

API_KEY = "ark-d75e387a-250c-44ad-aa00-a7afd271c206-af86e"

URL = "https://ark.cn-beijing.volces.com/api/v3/responses"

def chat(prompt: str) -> str:
    headers = {
        "Authorization": f"Bearer {API_KEY}",
        "Content-Type": "application/json",
    }

    data = {
        "model": "doubao-seed-2-0-pro-260215",
        "input": [
            {
                "role": "user",
                "content": [
                    {
                        "type": "input_text",
                        "text": prompt
                    }
                ]
            }
        ]
    }

    resp = requests.post(URL, headers=headers, json=data, timeout=60)

    print("status:", resp.status_code)
    print("resp:", resp.text)

    result = resp.json()

    if "error" in result:
        raise RuntimeError(result["error"])

    for item in result.get("output", []):
        if item.get("type") == "message":
            for c in item.get("content", []):
                if c.get("type") == "output_text":
                    return c.get("text", "")

    raise RuntimeError(f"没有在返回中找到模型文本输出: {result}")