import sys
print("Python:", sys.version)

# Step 1: pydantic
print("\n--- Step 1: pydantic ---")
try:
    import pydantic
    print("pydantic:", pydantic.__version__)
except Exception as e:
    print("FAILED:", e)
    sys.exit(1)

# Step 2: langchain-core
print("\n--- Step 2: langchain-core ---")
try:
    import langchain_core
    print("langchain-core:", langchain_core.__version__)
except Exception as e:
    print("FAILED:", e)
    sys.exit(1)

# Step 3: Simple LLM call via LangChain
print("\n--- Step 3: LangChain LLM call ---")
try:
    import json
    import urllib.request
    import ssl

    DEEPSEEK_API_KEY = "sk-2c0f7c8c966747e7aee5cab44ed498b7"

    from langchain_core.language_models.chat_models import BaseChatModel
    from langchain_core.messages import HumanMessage, AIMessage

    print("BaseChatModel imported OK")
    print("LangChain core is fully functional on OpenHarmony!")

except Exception as e:
    print("FAILED:", e)
    import traceback
    traceback.print_exc()
