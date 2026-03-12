"""AgentScope on OpenHarmony - real agent test with DeepSeek API."""
import sys
print(f"Python: {sys.version}")

import agentscope
print(f"agentscope: {agentscope.__version__}")

# Initialize with DeepSeek model config
agentscope.init(
    model_configs={
        "config_name": "deepseek",
        "model_type": "openai_chat",
        "model_name": "deepseek-chat",
        "api_key": "sk-2e11c41814eb45a1b87e15e1af4c18c3",
        "client_args": {
            "base_url": "https://api.deepseek.com/v1",
        },
        "generate_args": {
            "temperature": 0.7,
        },
    },
)

from agentscope.agents import DialogAgent

agent = DialogAgent(
    name="Assistant",
    sys_prompt="You are a helpful assistant running on OpenHarmony OS. Keep responses brief.",
    model_config_name="deepseek",
)

from agentscope.message import Msg

msg = Msg(name="user", content="Hello! What OS are you running on? Reply in one sentence.", role="user")
response = agent(msg)
print(f"\nAgent response: {response.content}")
print(f"Type: {type(response).__name__}")
print(f"\n=== AgentScope on OpenHarmony: SUCCESS ===")
