# 3DO ROM Tags

## Overview

ROM Tags are metadata structures on 3DO discs that identify special components like boot code, operating system files, signatures, and splash screens. They provide a boot-time lookup mechanism for the system to locate critical files without traversing the filesystem.
The definitions below match the Portfolio OS source tree mappings, especially
`portfolio_os/src/includes/rom.h` and `portfolio_os/src/dipir/cdipir.c`.

## ROM Tag Structure

### Memory Layout (24 bytes)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0x00 | 1 | `sub_systype` | `uint8_t` | Subsystem type |
| 0x01 | 1 | `type` | `uint8_t` | Component type |
| 0x02 | 1 | `version` | `uint8_t` | Component version |
| 0x03 | 1 | `revision` | `uint8_t` | Component revision |
| 0x04 | 1 | `flags` | `uint8_t` | ROM tag flags |
| 0x05 | 1 | `type_specific` | `uint8_t` | Type-specific data |
| 0x06 | 1 | `reserved1` | `uint8_t` | Reserved |
| 0x07 | 1 | `reserved2` | `uint8_t` | Reserved |
| 0x08 | 4 | `offset` | `uint32_t` | Block offset (0-indexed) |
| 0x0C | 4 | `size` | `uint32_t` | Size in bytes |
| 0x10 | 16 | `reserved3` | `uint32_t[4]` | Reserved / type-specific |

**Total size: 24 bytes**

## Subsystem Types

The `sub_systype` field identifies the subsystem category:

| Value | Name | Description |
|-------|------|-------------|
| `0x0F` | `RSANODE` | RSA-signable components on CD |
| `0x10` | `RT_SUBSYS_ROM` | Components in system ROM |

## Component Types (RSANODE Subsystem)

The `type` field when `sub_systype == RSANODE`:

### Boot and OS Components

| Value | Name | Description |
|-------|------|-------------|
| `0x01` | `RSA_MUST_RSA` | Must be RSA verified |
| `0x02` | `RSA_BLOCKS_ALWAYS` | Blocks always present |
| `0x03` | `RSA_BLOCKS_SOMETIMES` | Blocks sometimes present |
| `0x04` | `RSA_BLOCKS_RANDOM` | Random blocks |
| `0x05` | `RSA_SIGNATURE_BLOCK` | Block of MD5 digest signatures |
| `0x06` | `RSA_BOOT` | Old CD dipir tag (legacy) |
| `0x07` | `RSA_OS` | Operating system (sherry, operator, fs) |
| `0x08` | `RSA_CDINFO` | Optional mastering information |
| `0x09` | `RSA_NEWBOOT` | Old CD dipir, double key scheme |
| `0x0A` | `RSA_NEWNEWBOOT` | Old CD dipir, cheezo-encrypted |
| `0x0B` | `RSA_NEWNEWGNUBOOT` | Old CD dipir, doubly encrypted |
| `0x0C` | `RSA_BILLSTUFF` | Bill Duvall special request |
| `0x0D` | `RSA_NEWKNEWNEWGNUBOOT` | **Current boot code** (quadruple secure) |
| `0x0F` | `RSA_OLD_MISCCODE` | Old misc_code (C&B and Sampler) |
| `0x10` | `RSA_MISCCODE` | Current misc_code |
| `0x11` | `RSA_APP` | Start of application area |
| `0x12` | `RSA_DRIVER` | Downloadable device drivers |
| `0x13` | `RSA_DEVDIPIR` | Dipir driver for non-CD device |
| `0x14` | `RSA_APPSPLASH` | Application splash screen image |
| `0x15` | `RSA_DEPOTCONFIG` | Depot configuration file |
| `0x16` | `RSA_DEVICE_INFO` | Device ID & related info |
| `0x17` | `RSA_DEV_PERMS` | List of usable devices |
| `0x18` | `RSA_BOOT_OVERLAY` | Overlay module for RSA_NEW*BOOT |

### M2 Component Types

| Value | Name | Description |
|-------|------|-------------|
| `0x87` | `RSA_M2_OS` | M2 operating system |
| `0x90` | `RSA_M2_MISCCODE` | M2 misc code |
| `0x92` | `RSA_M2_DRIVER` | M2 dipir device driver |
| `0x93` | `RSA_M2_DEVDIPIR` | M2 device dipir |
| `0x94` | `RSA_M2_APPBANNER` | M2 application banner image |
| `0x95` | `RSA_M2_APP_KEYS` | 65-byte followed by 129-byte APP key |
| `0x96` | `RSA_OPERA_CD_IMAGE` | Opera CD image (Bridgit) |
| `0x97` | `RSA_M2_ICON` | Device icon image |

## Component Types (ROM Subsystem)

The `type` field when `sub_systype == RT_SUBSYS_ROM`:

### Boot Code Related

| Value | Name | Description |
|-------|------|-------------|
| `0x10` | `ROM_DIAGNOSTICS` | In-ROM diagnostics code |
| `0x11` | `ROM_DIAG_LOADER` | Loader for downloadable diagnostics |
| `0x12` | `ROM_VER_STRING` | Version string for static screen |

### Dipir Related

| Value | Name | Description |
|-------|------|-------------|
| `0x20` | `ROM_DIPIR` | System ROM dipir code |
| `0x21` | `ROM_DIPIR_DRIVERS` | Various dipir device drivers |

### OS Related

| Value | Name | Description |
|-------|------|-------------|
| `0x30` | `ROM_KERNEL_ROM` | Reduced kernel for ROM apps |
| `0x31` | `ROM_KERNEL_CD` | Full kernel for titles (no misc) |
| `0x32` | `ROM_OPERATOR` | Operator for ROM apps/titles |
| `0x33` | `ROM_FS` | Filesystem for ROM apps/titles |

### Configuration Related

| Value | Name | Description |
|-------|------|-------------|
| `0x40` | `ROM_SYSINFO` | System information code |
| `0x41` | `ROM_FS_IMAGE` | Mountable ROM filesystem |
| `0x42` | `ROM_PLATFORM_ID` | ROM release ID |
| `0x43` | `ROM_ROM2_BASE` | Base address of 2nd ROM bank |

## ROM Tag Location

ROM tags are stored immediately after the disc label:
```
romtags_block = disc_label_block + 1
```

## ROM Tag Termination

ROM tags are read sequentially until a terminator is found:
```c
while (true) {
    ROMTag tag;
    read(&tag, sizeof(tag));
    
    // Terminator condition
    if (tag.sub_systype == 0)
        break;
    
    // Process tag...
}
```

## Type-Specific Field Usage

### RSA_CDINFO (0x08)
| Field | Usage |
|-------|-------|
| `offset` | Zeros |
| `size` | Zeros |
| `reserved3[0]` | Copy of `VolumeUniqueId` |
| `reserved3[1]` | Random number (backup unique ID) |
| `reserved3[2]` | Date & time stamp |
| `reserved3[3]` | Reserved |

### RSA_SIGNATURE_BLOCK (0x05)
| Field | Usage |
|-------|-------|
| `type_specific` | Number of block digests to check (default: 15) |

### RSA_OS (0x07)
| Field | Usage |
|-------|-------|
| `version` | OS version (typically 24) |
| `revision` | OS revision (typically 225) |

### RSA_NEWKNEWNEWGNUBOOT (0x0D)
| Field | Usage |
|-------|-------|
| `version` | Boot code version (typically 2) |
| `revision` | Boot code revision (typically 5) |

### RSA_DRIVER / RSA_M2_DRIVER
| Field | Usage |
|-------|-------|
| `reserved1` | Component ID |

#### Component IDs
| Value | Name | Description |
|-------|------|-------------|
| 1 | `DDDID_LCCD` | LCCD driver |
| 2 | `DDDID_MICROCARD_MEM` | Microcard memory driver |
| 3 | `DDDID_PCMCIA_MEM` | 3DO card memory driver |
| 4 | `DDDID_HOST` | Debugger host FS driver |
| 5 | `DDDID_HOSTCD` | Host CD-ROM emulator driver |
| 6 | `DDDID_PCHOST` | PC debugger host FS driver |

### RSA_DEVDIPIR / RSA_M2_DEVDIPIR
| Field | Usage |
|-------|-------|
| `reserved1` | Dipir ID |

#### Dipir IDs
| Value | Name | Description |
|-------|------|-------------|
| 2 | `DIPIRID_CD` | CD-ROM media validation |
| 3 | `DIPIRID_ROMAPP` | Non-bootable RomApp device |
| 4 | `DIPIRID_SYSROMAPP` | Load RomApp OS from system ROM |
| 5 | `DIPIRID_HOST` | Load OS from debugger host |
| 6 | `DIPIRID_CART` | Load OS from bootable cartridge |
| 7 | `DIPIRID_BOOTROMAPP` | Load RomApp OS from device |
| 8 | `DIPIRID_MICROCARD` | Generic Microcard validation |
| 9 | `DIPIRID_VISA` | Generic VISA card validation |
| 10 | `DIPIRID_PC16550` | Proto PC16550 card validation |
| 11 | `DIPIRID_MEDIA_DEBUG` | Debug testing of RomApp media |
| 12 | `DIPIRID_PCDEVCARD` | PC developer card validation |

## Reading ROM Tags

```c
void read_romtags(DevStream &stream) {
    stream.data_block_seek(stream.romtags_block());
    
    while (true) {
        ROMTag tag;
        stream.read(tag);
        
        if (tag.sub_systype == 0)
            break;
        
        // Access the component
        uint32_t actual_block = tag.offset + 1;
        uint32_t size = tag.size;
        
        printf("Found %s at block %u, size %u bytes\n",
               tag.type_str(), actual_block, size);
    }
}
```

## File-to-ROMTag Mapping

The 3DO signing process maps specific filesystem paths to ROM tag types:

| File Path | ROM Tag Type |
|-----------|--------------|
| `signatures` | `RSA_SIGNATURE_BLOCK` |
| `system/kernel/boot_code` | `RSA_NEWKNEWNEWGNUBOOT` |
| `system/kernel/misc_code` | `RSA_MISCCODE` |
| `system/kernel/os_code` | `RSA_OS` |
| `bannerscreen` | `RSA_APPSPLASH` |

This table reflects the same file-to-ROMTag associations used during CD-DIPIR
load-time discovery in `portfolio_os/src/dipir/cdipir.c`.

## ROM Tags for Signatures

After signing, an additional RSA signature is appended after the ROM tags:

```
[DiscLabel][ROMTag][ROMTag]...[Terminator][RSA Signature (64 bytes)]
```

This signature covers: DiscLabel + ROMTags + boot_code data.
The same coverage is checked by the Portfolio OS boot path in
`portfolio_os/src/dipir/cdipir.c` (after the boot payload has been passed through
`DecryptBlock()`).
