import sys
print("Python:", sys.version)

import json
print("json: OK")

import ssl
print("ssl:", ssl.OPENSSL_VERSION)

import socket
print("socket: OK")

try:
    import pydantic_core
    print("pydantic_core:", pydantic_core.__version__)
except Exception as e:
    print("pydantic_core FAIL:", e)

try:
    import langchain_core
    print("langchain_core: OK")
except Exception as e:
    print("langchain_core FAIL:", e)

print("\n=== Deploy check done ===")
