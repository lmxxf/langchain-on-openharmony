import os, sys
os.write(1, b"1 skip hashlib/ssl, test pydantic\n")

# 阻止 _hashlib 和 _ssl 被 import
import importlib
class BlockSSL:
    def find_module(self, name, path=None):
        if name in ('_hashlib', '_ssl', 'ssl', '_blake2'):
            return self
    def load_module(self, name):
        raise ImportError(f"blocked: {name}")

sys.meta_path.insert(0, BlockSSL())
os.write(1, b"2 blocker installed\n")

import pydantic_core
os.write(1, b"3 pydantic_core ok\n")

import pydantic
os.write(1, b"4 pydantic ok\n")
