# 3DO OperaFS Disc Format

## Overview

The 3DO Interactive Multiplayer uses a proprietary disc format called OperaFS (also known as the Opera filesystem). This document describes the low-level structure of 3DO disc images.

## Disc Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DISC_BLOCK_SIZE` | 2048 | Standard data block size in bytes |
| `DISC_LABEL_OFFSET` | 225 | Byte offset to find the disc label marker |
| `DISC_LABEL_AVATAR_DELTA` | 32786 | Block offset between avatars |
| `DISC_LABEL_HIGHEST_AVATAR` | 7 | Maximum avatar index (0-7, total 8 avatars) |
| `DISC_TOTAL_BLOCKS` | 330000 | Maximum total blocks on disc |
| `FILESYSTEM_MAX_NAME_LEN` | 32 | Maximum filename length |

## Sector Formats

### Mode 1 Raw (2048 bytes)
Standard ISO 9660 Mode 1 sectors used by most 3DO images:

```
+------------------+
| Data (2048 bytes)|
+------------------+
```

- `device_block_header`: 0 bytes
- `device_block_data_size`: 2048 bytes
- `device_block_footer`: 0 bytes

### Mode 1 2352 (CD-ROM Mode 1)
Raw CD-ROM sectors with sync, header, and error correction:

```
+--------+--------+--------+------------------+--------+
| Sync   | Header | Sub-   | User Data        | ECC    |
| 12 bytes| 4 bytes| mode   | 2048 bytes       | 288    |
|        |        | 1 byte |                  | bytes  |
+--------+--------+--------+------------------+--------+
```

- `device_block_header`: 16 bytes (sync + header + sub-mode)
- `device_block_data_size`: 2048 bytes
- `device_block_footer`: 288 bytes (ECC/EDC)

#### Mode 1 Sync Pattern (12 bytes)
```
00 FF FF FF FF FF FF FF FF FF FF 00
```

#### Mode Byte Location
At offset 0x0F, the mode byte should be `0x01` for Mode 1.

## Volume Identification

### Record Types

| Value | Name | Description |
|-------|------|-------------|
| `0x01` | `RECORD_STD_VOLUME` | Standard volume record |
| `0xC2` | `RECORD_TINY_VOLUME` | Tiny volume record |

### Volume Sync Bytes
The disc label is identified by 5 consecutive sync bytes:
```
5A 5A 5A 5A 5A
```

### Volume Structure Versions

| Value | Name | Description |
|-------|------|-------------|
| `1` | `VOLUME_STRUCTURE_OPERA_READONLY` | Opera read-only filesystem |
| `2` | `VOLUME_STRUCTURE_LINKED_MEM` | Linked memory structure |
| `4` | `VOLUME_STRUCTURE_ACROBAT` | Acrobat structure |

### Volume Flags

| Mask | Name | Description |
|------|------|-------------|
| `0x01` | `VOLUME_FLAG_M2` | M2 (Konami) format flag |
| `0x02` | `VOLUME_FLAG_M2ONLY` | M2 only disc |
| `0x04` | `VOLUME_FLAG_DATADISC` | Data disc (won't auto-reboot) |
| `0x08` | `VOLUME_FLAG_BLESSED` | Blessed disc |

**Note:** `VOLUME_FLAG_M1_DATADISC` (0x01) is deprecated and conflicts with M2 flags.

## Disc Label Structure

The DiscLabel is the primary identification structure at the beginning of a 3DO disc.

### Memory Layout (132 bytes minimum)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0x00 | 1 | `record_type` | `char` | Should be `0x01` (`RECORD_STD_VOLUME`) |
| 0x01 | 5 | `volume_sync_bytes` | `char[5]` | Sync bytes: `5A 5A 5A 5A 5A` |
| 0x06 | 1 | `volume_structure_version` | `char` | Should be `0x01` |
| 0x07 | 1 | `volume_flags` | `char` | Volume flags (see above) |
| 0x08 | 32 | `volume_commentary` | `char[32]` | Human-readable disc description |
| 0x28 | 32 | `volume_identifier` | `char[32]` | Volume/disc name |
| 0x48 | 4 | `volume_unique_identifier` | `uint32_t` | Random unique ID (big-endian) |
| 0x4C | 4 | `volume_block_size` | `uint32_t` | Block size (typically 2048) |
| 0x50 | 4 | `volume_block_count` | `uint32_t` | Total blocks on disc |
| 0x54 | 4 | `root_unique_identifier` | `uint32_t` | Root directory unique ID |
| 0x58 | 4 | `root_directory_block_count` | `uint32_t` | Blocks in root directory |
| 0x5C | 4 | `root_directory_block_size` | `uint32_t` | Root directory block size |
| 0x60 | 4 | `root_directory_last_avatar_index` | `uint32_t` | Last avatar index (typically 7) |
| 0x64 | 32 | `root_directory_avatar_list` | `uint32_t[8]` | Avatar block pointers |

**Total size: 132 bytes (0x84)**

### Extended DiscLabel (M2 and later)

For M2 format discs, additional fields follow the base structure:

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0x84 | 4 | `num_rom_tags` | `uint32_t` | Number of ROM tags |
| 0x88 | 4 | `application_id` | `uint32_t` | Application identifier |
| 0x8C | 36 | `reserved` | `uint32_t[9]` | Reserved for future use |

**Extended total: 176 bytes**

### Avatar System

3DO uses an "avatar" system for redundancy and wear leveling. Each avatar is a copy of critical data:

- **Avatar Index**: Range 0-7 (8 avatars total)
- **Avatar Delta**: 32786 blocks between avatars
- **Purpose**: Provides fault tolerance - if one copy is damaged, another can be used

### Avatar List Interpretation

The `root_directory_avatar_list` contains up to 8 block pointers:
- Each pointer indicates a copy (avatar) of the root directory
- Avatar 0 is the primary copy
- Avatars 1-7 are redundant copies for fault tolerance
- `root_directory_last_avatar_index` indicates the highest valid avatar (typically 7)

## Finding the Disc Label

The disc label can be located by scanning for the volume signature:

1. Search for byte `0x01` (`RECORD_STD_VOLUME`)
2. Verify 5 consecutive `0x5A` bytes follow immediately
3. The label structure starts at the `0x01` byte

Alternatively, check sector 0 for Mode 1 2352 format:
1. Read first 12 bytes
2. Compare against sync pattern: `00 FF FF FF FF FF FF FF FF FF FF 00`
3. Check byte at offset 0x0F equals `0x01`

## Data Block Addressing

### Block Types

1. **Device Block**: Physical sector on disc
   - Size depends on sector format (2048 or 2352 bytes)
   
2. **Data Block**: Logical data block (always 2048 bytes)
   - Abstracted from physical format

### Address Calculations

For Mode 1 2352 sectors:
```
device_block = data_block
file_offset = (data_block * 2352) + 16  // skip sync + header
```

For Mode 1 Raw (2048 bytes):
```
device_block = data_block
file_offset = data_block * 2048
```

## Block Layout

```
Block 0:     Disc Label (primary avatar)
Block 1:     ROM Tags
Block 2+:    Root Directory (first avatar)
...          File data
Block N:     Disc Label (avatar 1)  // at offset DISC_LABEL_AVATAR_DELTA
...
```

### ROM Tags Location

ROM tags are stored in the block immediately following the disc label:
```
romtags_block = disc_label_block + 1
```

## Byte Order

All multi-byte integers in OperaFS structures are stored in **big-endian** format:
- `uint32_t` value `0x12345678` is stored as bytes: `12 34 56 78`
- Reading on little-endian systems requires byte swapping

## Special Disc Types

### Konami M2 Format

M2 discs are identified by:
1. `volume_flags` = `VOLUME_FLAG_M2 | VOLUME_FLAG_BLESSED` (0x09)
2. `volume_identifier` = `"cd-rom"`
3. Extended DiscLabel structure
4. Different ROM tag types (0x80+ range)

### ROMFS (Read-Only Memory Filesystem)

Identified by:
- `device_block_header` = 0
- `device_block_data_size` = 4
- `device_block_footer` = 0

ROMFS discs do not contain standard ROM tags.

## Checksums and Integrity

The 3DO system uses multiple integrity mechanisms:
1. **Avatar redundancy**: Multiple copies of critical structures
2. **MD5 signatures**: RSA-signed MD5 digests (see signing.md)
3. **Mode 1 ECC**: Error correction in 2352-byte sectors
