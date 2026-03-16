import sys
print("Python:", sys.version)

for mod in ['json', 'ssl', 'socket', '_struct', 'math', 'select', 'binascii']:
    try:
        __import__(mod)
        print(f"{mod}: OK")
    except Exception as e:
        print(f"{mod}: FAIL - {e}")

print("\n--- Rust extensions ---")
for mod in ['pydantic_core', 'jiter', 'uuid_utils']:
    try:
        __import__(mod)
        print(f"{mod}: OK")
    except Exception as e:
        print(f"{mod}: FAIL - {e}")

print("\n--- Pure Python ---")
for mod in ['pydantic', 'langchain_core']:
    try:
        __import__(mod)
        print(f"{mod}: OK")
    except Exception as e:
        print(f"{mod}: FAIL - {e}")

print("\nDone")
