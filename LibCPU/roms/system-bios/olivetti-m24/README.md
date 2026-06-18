# Olivetti M24 / AT&T PC 6300 — system BIOS ROMs

System BIOS images for the Olivetti M24 (a.k.a. AT&T PC 6300), for use with the `m24.dts`
machine description.

Source: <https://olivettim24.hadesnet.org/download.html> (files under `/dskimgs/`).

## Why LOW + HIGH halves

The M24 is built around a true 16-bit Intel 8086 with a 16-bit memory bus, so the BIOS is stored
in **two 8-bit ROM chips**: the **LOW** chip holds the even byte addresses, the **HIGH** chip the
odd ones. A running image is recovered by *interleaving* them:

```
combined[0::2] = LOW      # even addresses
combined[1::2] = HIGH     # odd addresses
```

Each half is 8 KiB, so a combined image is 16 KiB mapped at `F000:C000`–`F000:FFFF`. The reset
vector at `F000:FFF0` reads `EA 5B E0 00 F0` (`jmp F000:E05B`) in all three revisions.

## Files

| Revision | LOW            | HIGH           | Combined (16 KiB)     | Notes |
|----------|----------------|----------------|-----------------------|-------|
| 1.21     | `BIOS121L.ROM` | `BIOS121H.ROM` | `BIOS121-combined.ROM`| earliest |
| 1.36     | `BIOS136L.ROM` | `BIOS136H.ROM` | `BIOS136-combined.ROM`| extended hard-disk tables; better COM2/EGC and 8087 handling |
| 1.43     | `BIOS143L.ROM` | `BIOS143H.ROM` | `BIOS143-combined.ROM`| 720 KB 3.5" drive, Token Ring, EGA compatibility (dated 04/03/86) |

The `*L.ROM` / `*H.ROM` files are the raw chip dumps as distributed; the `*-combined.ROM` files
are the interleaved bootable images (derived — regenerate by interleaving the halves as above).

## Booting

```
lcx machine m24.dts --bios olivetti-m24/BIOS143-combined.ROM
```

(The combined image maps at the top of memory and runs from the reset vector. As with other real
BIOSes in this tree, POST is exploratory — see the BIOS-boot notes.)
