import os

# 逐个测 lib-dynload 里的 .so
mods = [
    '_struct', 'math', 'select', 'binascii', '_socket', '_json',
    '_datetime', '_random', '_pickle', '_asyncio', '_bisect',
    '_heapq', '_csv', '_posixsubprocess', '_contextvars',
    '_typing', '_queue', 'array', 'fcntl', 'mmap',
    '_opcode', '_statistics', 'unicodedata',
    '_sha256', '_sha512', '_sha1', '_sha3', '_md5',
]

for m in mods:
    try:
        __import__(m)
        os.write(1, f"{m}: OK\n".encode())
    except ImportError as e:
        os.write(1, f"{m}: IMPORT_ERR {e}\n".encode())
    except Exception as e:
        os.write(1, f"{m}: ERR {e}\n".encode())

os.write(1, b"\nDone\n")
