#!/usr/bin/env python3
import sys
CELL = 8
def main():
    words = {}
    args = sys.argv[1:]
    i = 0; org = 0
    while i < len(args):
        a = args[i]
        if a in ("--org", "--at"):
            org = int(args[i+1], 0); i += 2
            while i < len(args) and not args[i].startswith("--"):
                words[org] = int(args[i], 0); org += 1; i += 1
        else:
            i += 1
    hi = max(words) if words else 0
    buf = bytearray((hi + 1) * CELL)
    for idx, w in words.items():
        v = w & 0o777777
        for b in range(CELL):
            buf[idx*CELL + b] = (v >> (8*b)) & 0xff
    sys.stdout.buffer.write(buf)
main()
