"""Replace broken aiohttp 3.6.2 with a minimal stub that lets dashscope import."""
import os, shutil

sp = "/data/local/tmp/python-home/lib/python3.11/site-packages"
aiohttp_dir = os.path.join(sp, "aiohttp")

# Remove old broken aiohttp
if os.path.isdir(aiohttp_dir):
    shutil.rmtree(aiohttp_dir)
    print("Removed old aiohttp/")

# Create stub
os.makedirs(aiohttp_dir, exist_ok=True)

stub = '''"""Minimal aiohttp stub for OHOS - real aiohttp 3.6.2 is incompatible with Python 3.11.
Only provides enough symbols for dashscope to import without crashing.
Actual async HTTP calls will raise NotImplementedError."""

import enum

class WSMsgType(enum.IntEnum):
    CONTINUATION = 0x0
    TEXT = 0x1
    BINARY = 0x2
    PING = 0x9
    PONG = 0xA
    CLOSE = 0x8
    CLOSING = 0x100
    CLOSED = 0x101
    ERROR = 0x102

class ClientTimeout:
    def __init__(self, total=None, connect=None, sock_connect=None, sock_read=None):
        self.total = total

class ClientConnectorError(Exception):
    pass

class WSServerHandshakeError(Exception):
    pass

class ClientSession:
    def __init__(self, *args, **kwargs):
        raise NotImplementedError("aiohttp is not available on OHOS - use sync API instead")

class FormData:
    def __init__(self, *args, **kwargs):
        raise NotImplementedError("aiohttp FormData not available on OHOS")

class MultipartWriter:
    def __init__(self, *args, **kwargs):
        raise NotImplementedError("aiohttp MultipartWriter not available on OHOS")

class MultipartReader:
    @classmethod
    def from_response(cls, response):
        raise NotImplementedError("aiohttp MultipartReader not available on OHOS")

class ClientResponse:
    pass
'''

with open(os.path.join(aiohttp_dir, "__init__.py"), "w") as f:
    f.write(stub)

print("Created aiohttp stub")
