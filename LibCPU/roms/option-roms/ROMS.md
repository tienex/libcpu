# Option-ROM & firmware catalog

Firmware images for vintage PC/XT/AT expansion cards and systems -- disk
controllers (MFM/RLL/ESDI/SCSI/IDE), floppy controllers, video cards, network
cards, multifunction cards, and machine/motherboard BIOSes.

- **Source:** <https://www.minuszerodegrees.net/rom/rom.htm> (page saved here as
  `minuszerodegrees-rom.htm`), retrieved 2026-06-16.
- **Contents:** 363 ROM images across 65 vendors (6.8M), decompressed from the
  page's archives. Photos and notes that shipped inside the archives were dropped;
  only the ROM images are kept. A few images are alternate dump formats (`.hex`
  Intel HEX, `.Needham` programmer dump).

## Using a ROM as controller firmware

A controller's option ROM lives in the upper-memory area (0xC0000-0xEFFFF); the
system BIOS finds it by its `0x55 0xAA` signature. Two ways to load one:

- **Command line (auto-assigned address):**
  ```
  lcx machine pcxt.dts --rom st506="roms/option-roms/Seagate/Seagate ST11 - BIOS version 2.0.0.bin" \
                       --rom xtide="roms/option-roms/Acculogic/Acculogic sIDE-1 - ROM BIOS - 62-00352-801 IDE BIOS 3.1.bin"
  ```
  The device is matched by bundle/node name; the load address is auto-assigned to
  the next free 2 KiB option-ROM slot.

- **Device tree (fixed address):** a rom node with a firmware path (resolved
  relative to the .dts file):
  ```
  hdc-rom@c8000 {
      compatible = "libcpu,rom";
      reg = <0xc8000 0x4000>;
      libcpu,firmware = "../roms/option-roms/Seagate/Seagate ST11 - BIOS version 2.0.0.bin";
      read-only;
  };
  ```

## Device-type breakdown (from the source page)

| Count | Device type |
|------:|-------------|
| 38 | 8-bit MFM controller |
| 18 | 8-bit floppy controller |
| 9 | SCSI controller |
| 8 | 8-bit RLL controller |
| 7 | 16-bit SCSI controller |
| 6 | 8-bit SCSI controller |
| 4 | 8-bit SCSI card |
| 4 | 8-bit multifunction card |
| 4 | 8-bit IDE-XT controller |
| 3 | Display adapter |
| 3 | 8-bit video card |
| 3 | 8-bit network card |
| 3 | 8-bit IDE controller |
| 3 | 16-bit MFM controller |
| 2 | 16-bit RLL controller |
| 2 | 16-bit ESDI controller |
| 1 | Floppy controller |
| 1 | 16-bit video card |
| 1 | 16-bit network card |

## ROMs by vendor

### Acculogic (1)

- `Acculogic sIDE-1 - ROM BIOS - 62-00352-801 IDE BIOS 3.1.bin` — 8 KB

### Acer (1)

- `Acer Acros 486DX33 - BIOS - 486V6 V1.2R1.0.bin` — 128 KB

### Adaptec (7)

- `Adaptec ACB-2070A - BIOS ROM - '401402-00 A 1985'.bin` — 8 KB
- `Adaptec ACB-2070A - BIOS ROM - '405702-00 A 1986'.bin` — 8 KB
- `Adaptec ACB-2070A - ROM - '401401-00 C 1985'.bin` — 8 KB
- `Adaptec ACB-2070A - ROM - '405701-00 B 1986'.bin` — 8 KB
- `Adaptec ACB-2070A - ROM - '490043-00 A 1987'.bin` — 8 KB
- `Adaptec AHA-1542CF - 553601-00 C BIOS C38D.BIN` — 32 KB
- `Adaptec AHA-1542CF - 553801-00 C MCODE 563D.BIN` — 32 KB

### ASC (1)

- `ASC-88 - BIOS ROM - Revision 4.0N.bin` — 8 KB

### AST (6)

- `AST 3G - BIOS ROM V2.015.bin` — 16 KB
- `AST part number 107000-499 REV B.bin` — 16 KB
- `AST part number 107000-500 REV B.bin` — 16 KB
- `AST Xformer_286 - BIOS Version 1.20 - 27C128 - EVEN.bin` — 16 KB
- `AST Xformer_286 - BIOS Version 1.20 - 27C128 - ODD.bin` — 16 KB
- `AST-3G Plus II - BIOS ROM.bin` — 16 KB

### AT&T (2)

- `AT&T PC 6300 - BIOS ROM - Version 1.1 - High.bin` — 8 KB
- `AT&T PC 6300 - BIOS ROM - Version 1.1 - Low.bin` — 8 KB

### ATI (6)

- `ATI - EGA Wonder - BIOS ROM - Version 3.06.bin` — 32 KB
- `ATI - EGA Wonder 800 - BIOS ROM - Version 1.03.bin` — 32 KB
- `ATI - Mach64 ISA - 112-28124-102 EVEN - U1.bin` — 16 KB
- `ATI - Mach64 ISA - 112-28125-102 ODD - U71.bin` — 16 KB
- `ATI - Small Wonder Graphics Solution Version 1 (S27C64A-20N).BIN` — 8 KB
- `ATI Graphics Solution Plus - ROM.bin` — 8 KB

### Behavior Tech Computer Corporation (2)

- `Behavior Tech Computer Corporation - 1510H - VGA BIOS Dv2.15-35.bin` — 32 KB
- `Behavior Tech Computer Corporation - 1570 - VGA BIOS V1.05.bin` — 64 KB

### Centos (1)

- `Centos CI-1020 ROM BIOS - Version 3.30.bin` — 8 KB

### Cirrus Logic (1)

- `Cirrus Logic CL-GD5422 - BIOS ROM - Ver 1.00d.bin` — 32 KB

### Columbia Data Products (17)

- `MPC 4_33 U45 AM2732DC U116.BIN` — 4 KB
- `MPC 4_33 U46 AM2732DC U115.BIN` — 4 KB
- `MPC 4_33 U47 AM2732DC U114.BIN` — 4 KB
- `mpc 4_35 u77.BIN` — 8 KB
- `mpc 4_35 u78.BIN` — 8 KB
- `Mpc Vid 1_0 TMS2516JL-45 U42.BIN` — 2 KB
- `MPC_4.36_U45-A.bin` — 4 KB
- `MPC_4.36_U46-B.bin` — 4 KB
- `MPC_4.36_U47.bin` — 4 KB
- `MPC_VID-1.0.BIN` — 2 KB
- `MPC3_02_U45.BIN` — 4 KB
- `MPC3_02_U46.BIN` — 4 KB
- `MPC3_02_U47.BIN` — 4 KB
- `MPC3_02_U48.BIN` — 4 KB
- `MPC4.34_U45.BIN` — 4 KB
- `MPC4.34_U46.BIN` — 4 KB
- `MPC4.34_U47.BIN` — 4 KB

### Compaq (34)

- `Compaq - BIOS - Revision G - 105681-001.bin` — 8 KB
- `Compaq - BIOS - Revision H - 106265-001.bin` — 8 KB
- `Compaq - BIOS - Revision J - 106265-002.bin` — 8 KB
- `Compaq Portable - BIOS - Rev B.bin` — 8 KB
- `Compaq Portable - BIOS - Revision F - 100298-005.bin` — 8 KB
- `Compaq Portable - VDU board - ROM - 100519-001 REV B.bin` — 8 KB
- `Compaq Portable 286 U39 102667-002 27C128.BIN` — 16 KB
- `Compaq Portable 286 U94 102669-002 27C128.BIN` — 16 KB
- `Compaq Portable II - BIOS - Revision D - 105620-001 - U29 - Odd.bin` — 16 KB
- `Compaq Portable II - BIOS - Revision D - 105622-001 - U28 - Even.bin` — 16 KB
- `Compaq Portable II - BIOS - Revision G - 106580-001 - U29 - Odd.bin` — 16 KB
- `Compaq Portable II - BIOS - Revision G - 106581-001 - U28 - Even.bin` — 16 KB
- `Compaq Portable II - BIOS - Revision M - 106970-001 - U29 - Odd.bin` — 16 KB
- `Compaq Portable II - BIOS - Revision M - 106971-001 - U28 - Even.bin` — 16 KB
- `Compaq Portable II - BIOS - Revision P.1 - 109739-001 - U28 - Odd.bin` — 16 KB
- `Compaq Portable II - BIOS - Revision P.1 - 109740-001 - U28 - Even.bin` — 16 KB
- `Compaq Portable II - BIOS - Revision S.1 - 109739-003 - U29 - Odd.bin` — 16 KB
- `Compaq Portable II - BIOS - Revision S.1 - 109740-003 - U28 - Even.bin` — 16 KB
- `Compaq Portable III - BIOS - 106778-002 - Odd.bin` — 16 KB
- `Compaq Portable III - BIOS - 106779-002 - Even.bin` — 16 KB
- `Compaq Portable Plus - Hard Disk Controller - BIOS ROM - 100692-006.bin` — 8 KB
- `Compaq Portable Plus - Hard Disk Controller - BIOS ROM - 100693-4.bin` — 8 KB
- `Compaq Portable Plus 100666-001 Rev C u40.BIN` — 8 KB
- `Compaq Portable Plus 100666-001 Rev C u47.BIN` — 8 KB
- `Compaq SLT 386s_20 - BIOS - 27AUG1990 - EVEN - 118284-005.BIN` — 32 KB
- `Compaq SLT 386s_20 - BIOS - 27AUG1990 - ODD - 118283-005.BIN` — 32 KB
- `Compaq SLT286 - BIOS - Revision J.2 - Even.bin` — 32 KB
- `Compaq SLT286 - BIOS - Revision J.2 - Odd.bin` — 32 KB
- `P.2 Combined.bin` — 64 KB
- `P.2 Even 109738-001.bin` — 32 KB
- `P.2 Odd 109737-001.bin` — 32 KB
- `R.2 Combined.bin` — 64 KB
- `R.2 Even 109738-002.bin` — 32 KB
- `R.2 Odd 109737-002.bin` — 32 KB

### Cordata (1)

- `Cordata PC-400 - BIOS ROM - Version 4.33C.bin` — 16 KB

### Corel (1)

- `Corel LS2000 - BIOS ROM - Ver 1.65.bin` — 8 KB

### Corona (2)

- `Corona PPC-21 - BIOS ROM - Version 3.10.bin` — 8 KB
- `Corona PPC-21 - BIOS ROM - Version 4.23CG.bin` — 16 KB

### CTT (1)

- `SUNTEK hard disk BIOS - Version 1.22 - 2764.bin` — 8 KB

### Dell (3)

- `Dell System 200 - BIOS ROM - Version 3.10 A12 - Byte 0 (even).bin` — 32 KB
- `Dell System 200 - BIOS ROM - Version 3.10 A12 - Byte 1 (odd).bin` — 32 KB
- `Dell System 200 - Keyboard controller - 20575 B47-00.bin` — 2 KB

### DTC (10)

- `DTC BIOS BXD06.bin` — 8 KB
- `DTC BIOS BXD07.bin` — 8 KB
- `DTC BIOS CRD18A.bin` — 8 KB
- `DTC BIOS CRH16A.bin` — 16 KB
- `DTC BIOS CRL02A.bin` — 16 KB
- `DTC BIOS CRN15A.bin` — 16 KB
- `DTC BIOS CXD03A.bin` — 8 KB
- `DTC BIOS CXD04A.bin` — 8 KB
- `DTC BIOS CXD21A.bin` — 8 KB
- `DTC BIOS CXD23A.bin` — 8 KB

### DTK (2)

- `DTK PII-151 - BIOS version 1.04.bin` — 8 KB
- `DTK PII-151B - BIOS version 1.06B.bin` — 8 KB

### Eagle (8)

- `Eagle 1600 - BIOS - Version 2.2 - 62-2732-001 Rev E U403.BIN` — 4 KB
- `Eagle 1600 - BIOS - Version 2.2 - 62-2732-002 Rev E U404.BIN` — 4 KB
- `Eagle 1600 - Xebec SASI bridge 1046838 U11H.BIN` — 4 KB
- `Eagle Computer 62-2746-006 Rev B Copyright 1983 U401.bin` — 8 KB
- `Eagle PC PC-2 - CGA Card - Character ROM - U401.bin` — 4 KB
- `Eagle PC PC-2 BIOS 2.812 1986 U1101.BIN` — 8 KB
- `U1101.BIN` — 8 KB
- `U1103.BIN` — 8 KB

### Everex (5)

- `Everex EV-235 Ultragraphics Video BIOS V1.02.bin` — 16 KB
- `Everex EV-346 - BIOS ROM - Ver 3.2.bin` — 8 KB
- `Everex EV-390 - BIOS ROM - Ver 3.92A.bin` — 8 KB
- `Everex EV-391 - BIOS ROM - Ver 3.92.bin` — 8 KB
- `Everex EV-657 - BIOS ROM - Ver 1.30.bin` — 32 KB

### Future Domain (9)

- `Future Domain - 1800_18C50 SCSI ROM BIOS v3.2.bin` — 8 KB
- `Future Domain - 950 SCSI ROM BIOS v8.2.bin` — 8 KB
- `Future Domain - SCSI Disk Drive ROM BIOS V4.0I.bin` — 8 KB
- `Future Domain - SCSI Disk Drive ROM BIOS V6.01.bin` — 8 KB
- `Future Domain - SCSI ROM BIOS 950 AT V7.0.bin` — 8 KB
- `Future Domain - SCSI ROM BIOS for 18C30_18C50_1800 v3.4.bin` — 8 KB
- `Future Domain - SCSI ROM BIOS for 18C30_18C50_1800 v3.5.bin` — 8 KB
- `Future Domain - SCSI ROM BIOS for 9C50_950 V8.4.bin` — 8 KB
- `Future Domain - SCSI ROM BIOS for 9C50_950 v8.5.bin` — 8 KB

### GSI (1)

- `GSI1535.bin` — 8 KB

### IBM (15)

- `IBM 5150 - Cassette BASIC version C1.00 - U29 - 5700019.bin` — 8 KB
- `IBM 5150 - Cassette BASIC version C1.00 - U30 - 5700027.bin` — 8 KB
- `IBM 5150 - Cassette BASIC version C1.00 - U31 - 5700035.bin` — 8 KB
- `IBM 5150 - Cassette BASIC version C1.00 - U32 - 5700043.bin` — 8 KB
- `IBM 5150 - Cassette BASIC version C1.10 - U29 - 5000019.bin` — 8 KB
- `IBM 5150 - Cassette BASIC version C1.10 - U30 - 5000021.bin` — 8 KB
- `IBM 5150 - Cassette BASIC version C1.10 - U31 - 5000022.bin` — 8 KB
- `IBM 5150 - Cassette BASIC version C1.10 - U32 - 5000023.bin` — 8 KB
- `IBM 83-key keyboard - ROM code for 8048.bin` — 1 KB
- `IBM PGC - ROM - Fonts.bin` — 4 KB
- `IBM PGC - ROM - U43 - 59X7354.bin` — 32 KB
- `IBM PGC - ROM - U43 - 6137322.bin` — 32 KB
- `IBM PGC - ROM - U44 - 59X7355.bin` — 32 KB
- `IBM PGC - ROM - U44 - 6137323.bin` — 32 KB
- `IBM_5152_socket_1B.bin` — 4 KB

### International Quartz Ltd (4)

- `BIOS_HIGH.BIN` — 8 KB
- `BIOS_LOW.BIN` — 8 KB
- `KEYBOARD.BIN` — 4 KB
- `YAMAHA.BIN` — 4 KB

### Iomega (1)

- `Iomega floptical card - BIOS 01836200 V1.0.bin` — 32 KB

### Jameco (2)

- `Jameco JE1043 - BIOS ROM - Version E6610 C1.bin` — 8 KB
- `Jameco JE1043 - BIOS ROM - Version E6610 C2.bin` — 8 KB

### Joincom (1)

- `Joincom JC-1310 - BIOS ROM - Version APR-C 89.bin` — 8 KB

### Juko (2)

- `Juko D16X - BIOS version 1.2.bin` — 32 KB
- `Juko M16 - BIOS version 2.11.bin` — 8 KB

### Konan (2)

- `Konan KDC230 - ROM - PHX110B1.bin` — 8 KB
- `Konan KDC230 - ROM - PHX150G.bin` — 8 KB

### Leading Edge (4)

- `Leading Edge - Model M - BIOS ROM - Version 2.12.bin` — 16 KB
- `Leading Edge - Model M - BIOS ROM - Version 3.30.bin` — 16 KB
- `Leading Edge - Model M - BIOS ROM - Version 4.71.bin` — 16 KB
- `Leading Edge - Model M - ROM - Version 2.03.bin` — 4 KB

### Longshine (9)

- `Longshine LCS-6610F REV W1 - BIOS of revision U6.bin` — 8 KB
- `Longshine LCS-6814F - BIOS dated 05_25_1989.bin` — 8 KB
- `Longshine LCS-6821N - BIOS version 1.04.bin` — 8 KB
- `longshine_lcs6210c_rev_g.bin` — 8 KB
- `longshine_lcs6210d_rev_a1.bin` — 8 KB
- `longshine_lcs6210d_rev_e.bin` — 8 KB
- `longshine_lcs6220_old_version_of_card.bin` — 8 KB
- `longshine_lcs6220_rev_a3n_card.bin` — 8 KB
- `longshine_lcs6610f_rev_b_card.bin` — 8 KB

### Magitronic (1)

- `Magitronic B215 - BIOS ROM.bin` — 8 KB

### Micro Byte (1)

- `Micro Byte PC230 computer - Motherboard BIOS - Version 2.12.bin` — 32 KB

### Micro Solutions (1)

- `Micro Solutions - CompatiCard IV - BIOS ROM version 1.05.bin` — 8 KB

### Misc (73)

- `112-14318-102.bin` — 32 KB
- `112-14319-102.bin` — 32 KB
- `Abell - EN-100-1 - boot ROM.bin` — 8 KB
- `adaptec_acb2072_bios_408100_H_411503-00A.bin` — 8 KB
- `adaptec_acb2322d_bios_439201-00.bin` — 16 KB
- `adaptec_acb2322d_mcode_437302-00.bin` — 16 KB
- `adaptec_aha1542c_bios_534201-00.bin` — 32 KB
- `adaptec_aha1542c_mcode_534001-00.bin` — 32 KB
- `adaptec_aha1542cp_bios_908501-00.bin` — 32 KB
- `adaptec_aha1542cp_mcode_908301-00.bin` — 32 KB
- `adaptec_ava1515_bios_585201-00.bin` — 16 KB
- `ADP50L_218T.ROM` — 8 KB
- `Aquarius_Systems_AVGA-1DC_BIOS.bin` — 32 KB
- `ATI EGA Wonder 800+ N1.00.BIN` — 32 KB
- `ATI VGA Wonder+ BIOS version V1M-1.03_70 - EVEN chip.bin` — 32 KB
- `ATI VGA Wonder+ BIOS version V1M-1.03_70 - ODD chip.bin` — 32 KB
- `avga1-a11.bin` — 32 KB
- `BIOS_ITT_XTRA_5C00_V200_U92.BIN` — 16 KB
- `BIOS_ITT_XTRA_BF00_V200_U93.BIN` — 16 KB
- `BusLogic_BT-545S_U15_27128_5002026-4.50.bin` — 16 KB
- `BusLogic_BT-545S_U2_27256_5002005-3.31.bin` — 32 KB
- `D-Link DE-150.bin` — 8 KB
- `FA_100_floppy.BIN` — 8 KB
- `Hampden MCB-1.bin` — 2 KB
- `IBM 16_4 token ring card_25F9443_odd.bin` — 32 KB
- `IBM 16_4 token ring card_25F9444_even.bin` — 32 KB
- `ibm_1503033_5170_kyb_controller_on_motherboard.bin` — 2 KB
- `ibm_4860_1504036_XX1.bin` — 32 KB
- `ibm_4860_1504037_ZM63.bin` — 32 KB
- `ibm_5140_7396917.bin` — 32 KB
- `ibm_5140_7396918.bin` — 32 KB
- `IBM_5788005_AM9264_1981_CGA_MDA_CARD.BIN` — 8 KB
- `ibm_6277356_ega_card_u44_27128.bin` — 16 KB
- `ibm_vga.bin` — 32 KB
- `IBM_XEBEC_104648D.BIN` — 4 KB
- `IBM_XEBEC_104839E.BIN` — 4 KB
- `IBM_XEBEC_104839RE.BIN` — 4 KB
- `IBM_XEBEC_5000059_1982.BIN` — 8 KB
- `IBM_XEBEC_62X0822_1985.BIN` — 8 KB
- `IBM_XEBEC_6359121_1982.BIN` — 8 KB
- `interlan_np600a-3_u38.bin` — 8 KB
- `interlan_np600a-3_u39.bin` — 8 KB
- `K2000.ROM` — 8 KB
- `K2000MDF.ROM` — 8 KB
- `keytronic_101wn_xt_at_switchable.bin` — 2 KB
- `keytronic_kb3270pc_14166.bin` — 8 KB
- `keytronic_kb3270plus_55524.bin` — 8 KB
- `Kouwell KW-530D - By DTK - Version 1.01.bin` — 8 KB
- `Kouwell KW-530D - By JEC - 1987.bin` — 8 KB
- `MIDIMAN_MM401_1_15.BIN` — 8 KB
- `Novell Disk Coprocessor - 817-186-001 - REV E.bin` — 8 KB
- `PC10-III.4.41.BIOS.BIN` — 32 KB
- `PE-510D.bin` — 8 KB
- `Quadtel VGA BIOS Version 1.21.00.bin` — 29 KB
- `Rancho_RT1000_RTBios_version_8.10R.bin` — 8 KB
- `RTBIOS82.ROM` — 8 KB
- `samsung_samtron_88s_vers_2.0a.bin` — 32 KB
- `SSI-011SF_BIOS_v6.02.bin` — 8 KB
- `techway_multi_drive_ii_plus_vers_2.3_2764.bin` — 8 KB
- `televideo912b_rom_a3.bin` — 2 KB
- `televideo912b_rom_a49.bin` — 4 KB
- `trantor_t128_bios_v1.12.bin` — 8 KB
- `trantor_t130b_bios_v2.14.bin` — 8 KB
- `unique_fdc344_vers_4.2.bin` — 16 KB
- `unknown_vga_card_J6QVGAADAPTERIVI.bin` — 32 KB
- `video_seven_vega_vga_62L1989V5_435-0016-47.bin` — 32 KB
- `VTI-XTB version 2.0Y.bin` — 8 KB
- `wang_pc_250-16_bios_vers_03.13.00_chip_L46_9514ROH_HI.bin` — 32 KB
- `wang_pc_250-16_bios_vers_03.13.00_chip_L47_9514ROL_LO.bin` — 32 KB
- `WCT_NT-200B_BIOS_U23_LOW.bin` — 16 KB
- `WCT_NT-200B_BIOS_U24_HIGH.bin` — 16 KB
- `XLEVEN.BIN` — 32 KB
- `XLODD.BIN` — 32 KB

### NCL (4)

- `NCL - NDC-5027 - BIOS ROM - socket C1 - 2764 - CN-12-01.bin` — 8 KB
- `NCL - NDC-5027 - Microcode ROM - socket C6 - 2764 - 00-00-04.bin` — 8 KB
- `NCL - NDC5127 - BIOS ROM - socket E4 - 2764 - CN-17-01.bin` — 8 KB
- `NCL - NDC5127 - Microcode ROM - socket E2 - 2764 - 01-01-04.bin` — 8 KB

### NSI Logic (1)

- `NSI Logic - Smart EGA Plus - 04-086-01 Rev 1.45 - U21.bin` — 16 KB

### Oak (4)

- `Oak Technolgy VGA-KO77.bin` — 32 KB
- `Oak Technology VGA BIOS 067 V1.03.bin` — 32 KB
- `Oak Technology VGA BIOS Cv2.15-35 - via DEBUG.bin` — 32 KB
- `Oak Technology VGA BIOS Cv2.15-35 - via EPROM programmer.bin` — 32 KB

### Octek (4)

- `octek_unknown_1_BIOS_ROM_high.bin` — 32 KB
- `octek_unknown_1_BIOS_ROM_low.bin` — 32 KB
- `octek_unknown_2_BIOS_ROM_high.bin` — 32 KB
- `octek_unknown_2_BIOS_ROM_low.bin` — 32 KB

### Olivetti (7)

- `Olivetti M15 Plus - BIOS ROM - Version 1.10.bin` — 32 KB
- `Olivetti M24 - BIOS ROM - Version 1.43 - HIGH_ODD.bin` — 8 KB
- `Olivetti M24 - BIOS ROM - Version 1.43 - LOW_EVEN.bin` — 8 KB
- `Olivetti M24 - BIOS ROM - Version 1.44 - HIGH_ODD.bin` — 8 KB
- `Olivetti M24 - BIOS ROM - Version 1.44 - LOW_EVEN.bin` — 8 KB
- `Olivetti M240 - BIOS ROM - Version 2.04 - HIGH_ODD.bin` — 16 KB
- `Olivetti M240 - BIOS ROM - Version 2.04 - LOW_EVEN.bin` — 16 KB

### OMTI (1)

- `OMTI_5520A_M2764A.BIN` — 8 KB

### Orchid (1)

- `Orchid_Prodesigner_2_XL_translation_rom_nmc27c256b.BIN` — 32 KB

### Philips (3)

- `YES_23484.BIN` — 32 KB
- `YES_23494.BIN` — 32 KB
- `YES_23502.BIN` — 8 KB

### Priam (1)

- `PRIAM1.BIN` — 4 KB

### Quadtel (1)

- `Quadtel S3 86C801_86C805 Enhanced VGA BIOS - Version 2.13.01.bin` — 32 KB

### Seagate (11)

- `Seagate ST02 - BIOS version 3.2.bin` — 16 KB
- `Seagate ST02 - BIOS version 3.3.bin` — 16 KB
- `Seagate ST10 - BIOS version 2.4.bin` — 8 KB
- `Seagate ST11 - BIOS version 2.0.0.bin` — 16 KB
- `Seagate ST11 - BIOS version 2.1.bin` — 16 KB
- `Seagate ST11R - BIOS version 1.5.bin` — 16 KB
- `Seagate ST11R - BIOS version 1.7.bin` — 16 KB
- `Seagate ST11R - BIOS version 2.0.bin` — 16 KB
- `Seagate ST11R - Microcode version 1.5.bin` — 8 KB
- `Seagate ST21 - BIOS version 1.3.bin` — 16 KB
- `Seagate ST21 - Microcode version 1.7.bin` — 8 KB

### SMC (1)

- `SMC4003-PC - BIOS ROM.bin` — 8 KB

### Storage Plus (1)

- `Storage Plus SCSI-AT Rev. R1 - BIOS ROM - Version 6.3U.bin` — 8 KB

### Sunix (3)

- `Sunix SUN-4300 - BIOS ROM.bin` — 8 KB
- `Sunix SUN-4310 - BIOS ROM.bin` — 8 KB
- `Sunix SUN-6301 - BIOS ROM.bin` — 8 KB

### SyDOS (2)

- `SyDOS SCSI BIOS - Revision 3.3.2.bin` — 16 KB
- `SyDOS SCSI BIOS - Revision 3.3.5.bin` — 16 KB

### Sysgen (1)

- `Sysgen Omni-Bridge Floppy BIOS v2.00.bin` — 8 KB

### Toshiba (12)

- `T31L097B.BIN` — 32 KB
- `T31R098B.BIN` — 32 KB
- `Toshiba T1000 - BIOS ROM - V4.00.bin` — 32 KB
- `Toshiba T1000 - BIOS ROM - V4.10.bin` — 32 KB
- `Toshiba T1000 - ROM-DOS - R2A20US - Marked 9062.bin` — 256 KB
- `Toshiba T1000 - ROM-DOS - R2A22US - Marked B004.bin` — 512 KB
- `Toshiba T1000LE - BIOS ROM - Version V1.20.bin` — 64 KB
- `Toshiba T1200 - BIOS ROM - V4.00.bin` — 32 KB
- `Toshiba T2100 8086 - BIOS ROM - Version 004B.bin` — 32 KB
- `Toshiba T5100 - BIOS ROM - V2.30 - EVEN - 042F - 27C256.bin` — 32 KB
- `Toshiba T5100 - BIOS ROM - V2.30 - ODD - 043F - 27C256.bin` — 32 KB
- `Toshiba T5200C - BIOS ROM - V3.00.bin` — 128 KB

### Transteque (3)

- `Transteque HC-100 - BIOS ROM - Rev A.bin` — 8 KB
- `Transteque HC-100 - BIOS ROM - Ver 2.03.bin` — 8 KB
- `Transteque HC-1000 - BIOS ROM - Rev A.bin` — 8 KB

### Trident (6)

- `Trident 9000C MKII - BIOS ROM - Version C4.4.bin` — 32 KB
- `Trident TVGA8900D - BIOS ROM - Version C4.5.bin` — 32 KB
- `Trident TVGA9000A - BIOS ROM - Version D2.11.bin` — 32 KB
- `Trident TVGA9000I - BIOS ROM - Version D3.0.bin` — 32 KB
- `Trident TVGA9000I - BIOS ROM - Version D3.51.bin` — 32 KB
- `Trident TVGA9000I - BIOS ROM - Version D4.01E.bin` — 32 KB

### Tseng Labs (2)

- `Tseng Labs - ET3000AX - PN 8817 REV G - VGA BIOS V8.01.bin` — 32 KB
- `Tseng Labs VGA-4000 BIOS V1.1.bin` — 32 KB

### unknown (6)

- `Award EGA BIOS V1.6a.bin` — 32 KB
- `BIOS ROM - HFDC card Ver 1.60.bin` — 8 KB
- `Gemini VC-001 EGA BIOS - Version V2.4.bin` — 32 KB
- `MCT-VGA-16 - TDVGA 3588 BIOS Version V1.04A.bin` — 32 KB
- `VGA BIOS ROM - 27128 - HIGH.bin` — 16 KB
- `VGA BIOS ROM - 27128 - LOW.bin` — 16 KB

### Vendex (4)

- `Headstart LX - BIOS ROM - Ver 2.36.bin` — 32 KB
- `Vendex HeadStart Plus - BIOS ROM - Ver 2.0.bin` — 16 KB
- `Vendex Turbo 888 XT - ROM BIOS - VER 2.03A.bin` — 16 KB
- `Vendex Turbo 888 XT - ROM BIOS - VER 2.03C.bin` — 16 KB

### Video Seven (7)

- `Video Seven - Vega Deluxe - BIOS Dump - 435-0005-08 (C) 1987 VIDEO7.BIN` — 16 KB
- `Video Seven 435-0029-09 - Video7 - ver 1.09 - 27256.bin` — 32 KB
- `Video Seven 435-0030-09 - Video7 - ver 1.09 - 27128.bin` — 8 KB
- `Video Seven VGA 1024i - BIOS - v2.19 - 435-0061-05 - U16 - 27C128.BIN` — 16 KB
- `Video Seven VGA 1024i - BIOS - v2.19 - 435-0062-05 - U17 - 27C256.BIN` — 32 KB
- `Video Seven VGA 16E - BIOS ROM - 1.14 - EVEN.bin` — 16 KB
- `Video Seven VGA 16E - BIOS ROM - 1.14 - ODD.bin` — 16 KB

### VTech (3)

- `VTech - Laser Turbo XT - BIOS V1.11 - 27C64D.bin` — 8 KB
- `VTech - Laser XT3 - BIOS V1.26 - 27C64.bin` — 8 KB
- `VTech - XT Multi IO card - BIOS V1.05 - 27C64.bin` — 8 KB

### Wang (1)

- `wang_3050_BIOS_ROM.bin` — 32 KB

### Western Digital (21)

- `62-000042-013.bin` — 8 KB
- `62-000042-11.bin` — 4 KB
- `62-000043-010.bin` — 8 KB
- `62-000089-030.bin` — 8 KB
- `62-000094-002.bin` — 8 KB
- `62-000094-032.bin` — 8 KB
- `62-000100-003.bin` — 8 KB
- `62-000128-000.bin` — 8 KB
- `62-000215-060.bin` — 8 KB
- `62-000274-032.bin` — 8 KB
- `62-000279-061.bin` — 16 KB
- `62-000318-031.bin` — 16 KB
- `62-000352-031.bin` — 8 KB
- `Western Digital - FCC of DBM5UEPS2V00001 - 003056-002.bin` — 32 KB
- `Western Digital - FCC of DBM5UEPS2V00001.bin` — 32 KB
- `Western Digital - WD1002A-FOX - BIOS ROM.bin` — 8 KB
- `Western Digital - WD1002A-WX1 - BIOS ROM of unknown revision.bin` — 4 KB
- `Western Digital - WD1002S-WX2 - BIOS ROM of unknown revision.bin` — 8 KB
- `Western Digital - WD1004A-27X - BIOS ROM.bin` — 8 KB
- `Western Digital - WDXT-GEN2 PLUS - BIOS ROM of unknown revision.bin` — 8 KB
- `Western Digital WD1002-WX2 - IDEA BIOS ROM 8 7-26-85.bin` — 4 KB

### Wyse (2)

- `Dell - Wyse WY-2108 - Motherboard BIOS - Version 1.83 - Even - 27128.bin` — 16 KB
- `Dell - Wyse WY-2108 - Motherboard BIOS - Version 1.83 - Odd - 27128.bin` — 16 KB

### Xebec (4)

- `Xebec 1220 - BIOS ROM - 104830A - MCM68766.bin` — 8 KB
- `Xebec 1220 - CPU ROM - TMS2732A.bin` — 4 KB
- `xebec_1210c_u2.bin` — 8 KB
- `xebec_1210c_u44_104868C.bin` — 8 KB

### Zenith (8)

- `444-671-1-EVEN.BIN` — 32 KB
- `444-672-1-ODD.BIN` — 32 KB
- `HN27C256G - 444-459 - Character ROM.BIN` — 32 KB
- `HN27C256G - BIOS.BIN` — 32 KB
- `M27C64 - 444-415-1 - KB ROM.BIN` — 8 KB
- `MBM27C64 - 444-414 - Display Offset.BIN` — 8 KB
- `ZWL-184-02_10D_MB01Bv3.1.bin` — 32 KB
- `ZWL-184-02_11D_HB20Bv2.4.bin` — 8 KB

