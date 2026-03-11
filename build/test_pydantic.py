import sys
print("Python:", sys.version)

try:
    import pydantic_core
    print("pydantic_core imported OK!")
    print("VERSION:", pydantic_core.__version__)
except Exception as e:
    print("FAILED:", e)
