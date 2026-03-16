print("a")
import ctypes
print("b - ctypes ok")
try:
    lib = ctypes.CDLL("/data/local/tmp/lib/libcrypto.so.3")
    print("c - libcrypto loaded")
except Exception as e:
    print("c - libcrypto FAIL:", e)
try:
    lib2 = ctypes.CDLL("/data/local/tmp/lib/libssl.so.3")
    print("d - libssl loaded")
except Exception as e:
    print("d - libssl FAIL:", e)
