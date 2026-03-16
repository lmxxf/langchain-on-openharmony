import sys
import importlib
print("a")

# 直接加载 _ssl.so 看具体报什么错
try:
    spec = importlib.util.find_spec('_ssl')
    print("b - _ssl spec:", spec)
except Exception as e:
    print("b - find_spec fail:", e)

# 试试不走 ssl，测其他 .so 模块
for mod in ['_struct', 'math', 'select', 'binascii', '_socket', '_json', '_datetime', '_hashlib']:
    try:
        __import__(mod)
        print(f"{mod}: OK")
    except Exception as e:
        print(f"{mod}: FAIL - {e}")
