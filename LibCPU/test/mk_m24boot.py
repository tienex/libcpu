#!/usr/bin/env python3
# Generate a 360 KiB (40 cyl x 2 heads x 9 sectors x 512 B) diskette image whose boot sector prints
# "M24 BOOTS LibCPU!" via INT 10h teletype and then halts. Used by the machine.m24boot test to drive
# the Olivetti M24 BIOS all the way through POST, the INT 13h boot-strap, and into guest boot code.
#
# Usage: mk_m24boot.py <output-image-path>
#
# Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.

import sys

# Real-mode boot code, loaded by the BIOS at 0000:7C00. DS=0; SI -> the message; INT 10h/AH=0Eh each
# character; HLT when done. The message offset (0x7C19) is the byte just past this code.
CODE = bytes([
    0xFA,              # cli
    0x31, 0xC0,        # xor ax, ax
    0x8E, 0xD8,        # mov ds, ax
    0xBE, 0x19, 0x7C,  # mov si, 0x7C19            ; -> msg
    0xAC,              # print:  lodsb
    0x08, 0xC0,        # or al, al
    0x74, 0x09,        # jz done
    0xB4, 0x0E,        # mov ah, 0x0E             ; teletype
    0xBB, 0x07, 0x00,  # mov bx, 0x0007           ; page 0, attr 7
    0xCD, 0x10,        # int 0x10
    0xEB, 0xF2,        # jmp print
    0xF4,              # done:   hlt
    0xEB, 0xFD,        # jmp done
])
MSG = b"M24 BOOTS LibCPU!\r\n\x00"

def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: mk_m24boot.py <output-image-path>\n")
        return 2
    sector = bytearray(512)
    sector[0:len(CODE)] = CODE
    sector[len(CODE):len(CODE) + len(MSG)] = MSG
    sector[510] = 0x55                                   # boot signature
    sector[511] = 0xAA
    image = bytearray(40 * 2 * 9 * 512)
    image[0:512] = sector
    with open(sys.argv[1], "wb") as f:
        f.write(image)
    return 0

if __name__ == "__main__":
    sys.exit(main())
