

This is one of a few revisions of motherboard BIOS for the IBM 5160 (IBM XT).
This revision is dated 16AUG82 (08/16/82).


+---------------------------------+
|  GENERAL                        |
+---------------------------------+

ROM chip U18 has the IBM part number of 5000026, and is 32K in size
ROM chip U19 has the IBM part number of 5000027, and is  8K in size

8-bit checksum of 5000026 = 00
8-bit checksum of 5000027 = 00
                           ----
                    added = 00


+---------------------------------+
|  FILES                          |
+---------------------------------+

File BIOS_5160_16AUG82_U18_5000026.BIN is a binary image of ROM chip U18.  [ 32 KB in size ]
File BIOS_5160_16AUG82_U19_5000027.BIN is a binary image of ROM chip U19.  [  8 KB in size ]


+---------------------------------+
|  USING FILES TO MAKE EPROMS     |
+---------------------------------+

1. Obtain two blank 27256, or 27C256, EPROMs rated at 250 ns (or less [faster]).
   Avoid the NEC µPD27C256, an EPROM known not to work on early 5160 motherboards (where ROM sockets have pins 1 and 27 tied).

2. Burn file BIOS_5160_16AUG82_U18_5000026.BIN to the first 27256, then label the 27356 as "U18".

3. Burn file BIOS_5160_16AUG82_U19_5000027.BIN to the first quarter of the second 27256, then label the 27356 as "U19".
