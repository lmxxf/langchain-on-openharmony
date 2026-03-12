"""AgentScope 1.0.16 on OpenHarmony 6.0 (RK3568) — Full Agent Test"""
import sys, asyncio
print(f"Python: {sys.version}")

import agentscope
print(f"agentscope: {agentscope.__version__}")

agentscope.init()

from agentscope.model._openai_model import OpenAIChatModel

model = OpenAIChatModel(
    model_name="deepseek-chat",
    api_key="sk-e1747930417044149d12302141c9e4fa",
    stream=False,
    client_kwargs={"base_url": "https://api.deepseek.com/v1"},
    generate_kwargs={"temperature": 0.7},
)

async def main():
    response = await model(messages=[{"role": "user", "content": "Hello from OpenHarmony! Say hi in one sentence."}])
    # ChatResponse.content is a list of blocks
    text = response.content[0]["text"] if response.content else "(empty)"
    print(f"\nDeepSeek response: {text}")
    print(f"Tokens: {response.usage.input_tokens} in, {response.usage.output_tokens} out")
    print(f"\n=== AgentScope 1.0.16 + DeepSeek on OpenHarmony 6.0: SUCCESS ===")

asyncio.run(main())
