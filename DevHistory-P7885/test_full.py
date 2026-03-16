import os
os.write(1, b"1 stdlib\n")
import sys, json, ssl, socket, hashlib
os.write(1, f"  Python {sys.version}\n  ssl: {ssl.OPENSSL_VERSION}\n  sha256: {hashlib.sha256(b'hello').hexdigest()[:16]}...\n".encode())

os.write(1, b"2 pydantic\n")
import pydantic_core, pydantic
os.write(1, f"  pydantic_core: {pydantic_core.__version__}\n".encode())

os.write(1, b"3 langchain\n")
import langchain_core
os.write(1, b"  langchain_core: OK\n")

os.write(1, b"\n=== All imports OK ===\n")
