"""
LangChain on OpenHarmony 6.0 - Final Test
Use openai SDK directly with LangChain message types
"""
import sys
print("Python:", sys.version)

from langchain_core.messages import HumanMessage, AIMessage
print("langchain-core: OK")

import openai
print("openai SDK: OK")

client = openai.OpenAI(
    api_key=open("/data/local/tmp/.api-key").read().strip(),
    base_url="https://api.deepseek.com/v1",
)

# Build request using LangChain message format
messages = [HumanMessage(content="Say hello in one sentence.")]

# Convert LangChain messages to OpenAI format
openai_messages = [{"role": "user", "content": m.content} for m in messages]

print("Calling DeepSeek via OpenAI SDK...")
response = client.chat.completions.create(
    model="deepseek-chat",
    messages=openai_messages,
    max_tokens=50,
    temperature=0,
)

# Convert back to LangChain message
ai_message = AIMessage(content=response.choices[0].message.content)
print("Response:", ai_message.content)
print("Type:", type(ai_message).__name__)
print("\n=== LangChain + OpenAI SDK on OpenHarmony: SUCCESS ===")
