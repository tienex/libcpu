# ROM collection

Vintage PC firmware (BIOS and option/adapter ROMs) used by the LibCPU machine emulator. These
are the real ROM images a device-tree `rom@...` / option node can be backed by, loaded with
`lcx machine <machine.dts> --image <rom> --load <addr>`.

## Provenance

Mirrored from **minuszerodegrees.net** (Joachim Buzzelli's IBM PC documentation site):

- System board BIOS: <https://www.minuszerodegrees.net/bios/bios.htm>
- Option / adapter ROMs and clone BIOSes: <https://www.minuszerodegrees.net/rom/rom.htm>

These are 1980s firmware images preserved for hardware documentation and emulation. They remain
the copyright of their respective vendors (IBM, Compaq, Adaptec, Western Digital, ...); they are
included here solely to drive the emulator against authentic hardware behaviour.

## Layout (by category)

```
system-bios/        IBM system board BIOS, by machine
  ibm-5150/           IBM PC          (8 KB)
  ibm-5160/           IBM PC/XT       (8 KB U19 + 32 KB U18, and later 32+32 KB revisions)
  ibm-5162/           IBM PC/XT 286
  ibm-5170/           IBM PC/AT       (286)
option-roms/        option/adapter ROMs and clone/compatible system BIOSes, by vendor
  IBM/                IBM Cassette BASIC, CGA/MDA char ROM, EGA, VGA, XEBEC, PGC, keyboard
  Adaptec/ Seagate/ Western Digital/ Future Domain/ DTC/ Xebec/ ...  disk & SCSI controllers
  ATI/ Trident/ Tseng Labs/ Oak/ Video Seven/ Cirrus Logic/ ...      video adapters
  Compaq/ Toshiba/ Olivetti/ AST/ Zenith/ Eagle/ ...                 compatible-system BIOSes
  Misc/               ROMs the source listed without a vendor folder (name encodes the part)
```

Most files are raw `.bin` / `.rom` images; some are `.zip` archives (as distributed by the
source) containing the `.bin` plus a `README.TXT` and occasionally a chip photo.

## Relevance to the PC/XT emulator

The directly useful images for the current `test/pcxt.dts` machine are under `system-bios/ibm-5160/`
(the XT BIOS) and `option-roms/IBM/` (the CGA/MDA character ROM, the XEBEC fixed-disk ROM, and
IBM Cassette BASIC). The remainder are kept for building and testing other machine descriptions.
