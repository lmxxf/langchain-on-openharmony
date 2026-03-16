import os, sys

os.write(1, f"Python: {sys.version}\n".encode())

from langchain_core.messages import HumanMessage, AIMessage
os.write(1, b"langchain_core: OK\n")

import openai
os.write(1, b"openai SDK: OK\n")

os.write(1, b"Calling DeepSeek...\n")
client = openai.OpenAI(
    api_key="sk-e1747930417044149d12302141c9e4fa",
    base_url="https://api.deepseek.com/v1"
)

resp = client.chat.completions.create(
    model="deepseek-chat",
    messages=[{"role": "user", "content": "Say hello from OpenHarmony in one sentence."}],
    max_tokens=50
)

text = resp.choices[0].message.content
msg = AIMessage(content=text)
os.write(1, f"Response: {text}\nType: {type(msg).__name__}\n".encode())
os.write(1, b"\n=== LangChain + OpenAI SDK on OpenHarmony: SUCCESS ===\n")
