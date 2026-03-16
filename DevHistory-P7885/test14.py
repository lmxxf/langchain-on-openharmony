import os
os.write(1, b"1\n")

# 手动 dlopen libcrypto
import ctypes.util
os.write(1, b"2\n")
