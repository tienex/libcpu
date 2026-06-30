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
def emit_rim():
    args = sys.argv[2:]  # skip --rim
    out = bytearray()
    def frame_word(w):
        w &= 0o777777
        for sh in (12, 6, 0):
            out.append(0o200 | ((w >> sh) & 0o77))
    i = 0
    while i < len(args):
        if args[i] == "--dio":
            a = int(args[i+1], 0); w = int(args[i+2], 0)
            frame_word(0o320000 | (a & 0o7777)); frame_word(w); i += 3
        elif args[i] == "--jmp":
            a = int(args[i+1], 0); frame_word(0o600000 | (a & 0o7777)); i += 2
        else:
            i += 1
    sys.stdout.buffer.write(out)
if len(sys.argv) > 1 and sys.argv[1] == "--rim":
    emit_rim()
else:
    main()
