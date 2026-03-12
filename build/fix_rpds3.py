"""Fix rpds stub v3"""
import os
SP = "/data/local/tmp/python-home/lib/python3.11/site-packages"

rpds_init = os.path.join(SP, "rpds", "__init__.py")
with open(rpds_init, "w") as f:
    f.write('''"""rpds stub - minimal implementation for referencing/jsonschema"""

class HashTrieMap(dict):
    @classmethod
    def convert(cls, mapping=None):
        return cls(mapping) if mapping else cls()
    def insert(self, k, v):
        new = HashTrieMap(self)
        new[k] = v
        return new
    def remove(self, k):
        new = HashTrieMap(self)
        if k in new:
            del new[k]
        return new

class HashTrieSet(set):
    @classmethod
    def convert(cls, iterable=()):
        return cls(iterable)
    def insert(self, v):
        new = HashTrieSet(self)
        new.add(v)
        return new
    def remove(self, v):
        new = HashTrieSet(self)
        new.discard(v)
        return new
    def update(self, other):
        new = HashTrieSet(self)
        set.update(new, other)
        return new
    def __or__(self, other):
        return HashTrieSet(set.__or__(self, other))

class List(tuple):
    @classmethod
    def convert(cls, iterable=()):
        return cls(iterable)
    def push_front(self, v):
        return List((v,) + self)
    def drop_first(self):
        return List(self[1:])
    @property
    def first(self):
        return self[0] if self else None
    @property
    def rest(self):
        return List(self[1:])
''')
print("rpds stub v3 fixed")
