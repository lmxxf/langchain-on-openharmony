"""Fix numpy stub - make it importable but raise on actual usage"""
import os
import sys

SP = "/data/local/tmp/python-home/lib/python3.11/site-packages"

# Fix numpy stub: don't raise on import, raise on usage
numpy_init = os.path.join(SP, "numpy", "__init__.py")
with open(numpy_init, "w") as f:
    f.write('''"""numpy stub for OpenHarmony"""
import warnings as _w
_w.warn("numpy not available on OpenHarmony, some features disabled", ImportWarning)

__version__ = "0.0.0-stub"

class _Stub:
    def __getattr__(self, name):
        raise ImportError(f"numpy.{name} not available on OpenHarmony")
    def __call__(self, *a, **kw):
        raise ImportError("numpy not available on OpenHarmony")

int16 = float32 = float64 = int32 = int64 = _Stub()

def frombuffer(*a, **kw): raise ImportError("numpy.frombuffer not available")
def clip(*a, **kw): raise ImportError("numpy.clip not available")
def save(*a, **kw): raise ImportError("numpy.save not available")
def load(*a, **kw): raise ImportError("numpy.load not available")
def array(*a, **kw): raise ImportError("numpy.array not available")
''')
print("numpy stub fixed")

# Fix tiktoken_ext: copy from source or create minimal
tiktoken_ext = os.path.join(SP, "tiktoken_ext")
os.makedirs(tiktoken_ext, exist_ok=True)
init_file = os.path.join(tiktoken_ext, "__init__.py")
if not os.path.exists(init_file):
    with open(init_file, "w") as f:
        f.write("")
    print("tiktoken_ext __init__.py created")

# openai_public.py is needed by tiktoken
openai_public = os.path.join(tiktoken_ext, "openai_public.py")
if not os.path.exists(openai_public):
    with open(openai_public, "w") as f:
        f.write('''"""tiktoken_ext.openai_public - encoding definitions"""
import tiktoken

ENCODING_CONSTRUCTORS = {
    "cl100k_base": lambda: {
        "pat_str": r"""(?i:'s|'t|'re|'ve|'m|'ll|'d)|[^\\r\\n\\p{L}\\p{N}]?\\p{L}+|\\p{N}{1,3}| ?[^\\s\\p{L}\\p{N}]+[\\r\\n]*|\\s*[\\r\\n]+|\\s+(?!\\S)|\\s+""",
        "mergeable_ranks": {},
        "special_tokens": {"<|endoftext|>": 100257, "<|fim_prefix|>": 100258, "<|fim_middle|>": 100259, "<|fim_suffix|>": 100260, "<|endofprompt|>": 100276},
    },
}
''')
    print("tiktoken_ext/openai_public.py created")

print("ALL FIXES DONE")
