import os
os.write(1, b"1\n")
import _struct
os.write(1, b"2 _struct ok\n")
import math
os.write(1, b"3 math ok\n")
import select
os.write(1, b"4 select ok\n")
import binascii
os.write(1, b"5 binascii ok\n")
import _socket
os.write(1, b"6 _socket ok\n")
import _json
os.write(1, b"7 _json ok\n")
import _datetime
os.write(1, b"8 _datetime ok\n")
import _hashlib
os.write(1, b"9 _hashlib ok\n")
import _ssl
os.write(1, b"10 _ssl ok\n")
