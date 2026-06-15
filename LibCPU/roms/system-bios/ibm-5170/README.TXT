

This is one of a few revisions of motherboard BIOS for the IBM 5170 (IBM AT).
This revision is dated 10JUN85 (06/10/85).
It is found in type 2 motherboards (type 2 runs at 6 MHz).


+---------------------------------+
|  GENERAL                        |
+---------------------------------+

ROM chip U27 has the IBM part number of 6480090, and is 32K in size
ROM chip U47 has the IBM part number of 6480091, and is 32K in size


8 bit checksum of 6480090 = 71
8 bit checksum of 6480091 = 8F
                           ----
                    added = 00


+---------------------------------+
|  FILES                          |
+---------------------------------+

File BIOS_5170_10JUN85_U27_6480090_27256.BIN is a binary image of ROM chip U27.  [ 32 KB in size ]
File BIOS_5170_10JUN85_U47_6480091_27256.BIN is a binary image of ROM chip U47.  [ 32 KB in size ]


+---------------------------------+
|  USING FILES TO MAKE EPROMS     |
+---------------------------------+

1. Obtain two blank 27256, or 27C256.
   - 6 MHz 5170 motherboard: Ones rated at a maximum access time of 260 ns. (Or less, e.g. 250 ns.)
   - 8 MHz 5170 motherboard: Ones rated at a maximum access time of 190 ns. (Or less, e.g. 150 ns.)

2. Burn file BIOS_5170_10JUN85_U27_6480090_27256.BIN to the first 27256/27C256, then label the 27256/27C256 as "U27".

3. Burn file BIOS_5170_10JUN85_U47_6480091_27256.BIN to the second 27256/27C256, then label the 27256/27C256 as "U47".









