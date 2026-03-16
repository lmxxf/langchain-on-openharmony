import os
os.write(1, b"1\n")
import hashlib
os.write(1, b"2 hashlib ok\n")
h = hashlib.sha256(b"hello").hexdigest()
os.write(1, f"3 sha256={h}\n".encode())
import json
os.write(1, b"4 json ok\n")
import pydantic_core
os.write(1, b"5 pydantic_core ok\n")
import pydantic
os.write(1, b"6 pydantic ok\n")
import langchain_core
os.write(1, b"7 langchain_core ok\n")
