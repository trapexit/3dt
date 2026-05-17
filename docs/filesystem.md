# 3DO OperaFS Filesystem Structure

## Overview

OperaFS is the proprietary filesystem used by the 3DO Interactive Multiplayer. It uses a hierarchical directory structure with unique features like avatars for redundancy.

## Directory Header Structure

Each directory block begins with a DirectoryHeader.

### Memory Layout (20 bytes)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0x00 | 4 | `next_block` | `int32_t` | Next directory block (-1 if last) |
| 0x04 | 4 | `prev_block` | `int32_t` | Previous directory block (-1 if first) |
| 0x08 | 4 | `flags` | `uint32_t` | Directory flags |
| 0x0C | 4 | `first_free_byte` | `uint32_t` | Offset to first free byte in block |
| 0x10 | 4 | `first_entry_offset` | `uint32_t` | Offset to first directory entry |

**Total size: 20 bytes**

### Directory Header Fields

#### `next_block`
- Block number of the next directory block in the chain
- Value of `-1` indicates this is the last block
- Allows directories to span multiple blocks

#### `prev_block`
- Block number of the previous directory block
- Value of `-1` indicates this is the first block
- Enables bidirectional traversal

#### `first_free_byte`
- Offset from start of block to first unused byte
- Used when adding new entries
- Updated after each entry addition

#### `first_entry_offset`
- Offset from start of block to the first DirectoryRecord
- Typically follows the header immediately

## Directory Record Structure

DirectoryRecord describes files and subdirectories.

### Memory Layout (68 bytes fixed + avatars)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0x00 | 4 | `flags` | `uint32_t` | Entry flags (see below) |
| 0x04 | 4 | `unique_identifier` | `uint32_t` | Unique entry ID |
| 0x08 | 4 | `type` | `uint32_t` | Entry type (4CC or hex) |
| 0x0C | 4 | `block_size` | `uint32_t` | Block size for this entry |
| 0x10 | 4 | `byte_count` | `uint32_t` | File size in bytes |
| 0x14 | 4 | `block_count` | `uint32_t` | Blocks allocated |
| 0x18 | 4 | `burst` | `uint32_t` | Burst parameter |
| 0x1C | 4 | `gap` | `uint32_t` | Gap parameter |
| 0x20 | 32 | `filename` | `char[32]` | Null-terminated filename |
| 0x40 | 4 | `last_avatar_index` | `uint32_t` | Highest avatar index |
| 0x44 | 4*N | `avatar_list` | `uint32_t[]` | Avatar block pointers (N = last_avatar_index + 1) |

**Minimum size: 72 bytes (with 1 avatar)**

### Directory Record Flags

| Mask | Name | Description |
|------|------|-------------|
| `0x00000001` | `DR_FLAG_IS_DIRECTORY` | Entry is a directory |
| `0x00000002` | `DR_FLAG_IS_READONLY` | Entry is read-only |
| `0x00000004` | `DR_FLAG_IS_FOR_FILESYSTEM` | Entry is for filesystem use |
| `0x40000000` | `DR_FLAG_LAST_IN_BLOCK` | Last entry in this block |
| `0x80000000` | `DR_FLAG_LAST_IN_DIR` | Last entry in directory |

### Directory Record Types

The `type` field is a 4-byte value, often a FourCC (4-character code):

| Value | FourCC | Name | Description |
|-------|--------|------|-------------|
| `0x2A646972` | `*dir` | `DR_TYPE_DIRECTORY` | Directory entry |
| `0x2A6C626C` | `*lbl` | `DR_TYPE_LABEL` | Label entry |
| `0x2A7A6170` | `*zap` | `DR_TYPE_CATAPULT` | Catapult entry |

**Note:** FourCC values are stored in little-endian on disc. The `type_str()` function reads bytes in reverse order (indices 3,2,1,0) so the character representation reads left-to-right in ASCII.

### Avatar System in Directory Records

Files use avatars for data redundancy:

- **`last_avatar_index`**: Range 0-7, indicates how many avatars exist
- **`avatar_list`**: Array of block pointers, one per avatar
- Avatar 0 (`avatar_list[0]`) is the primary data location
- Avatars 1-7 provide redundant copies

### Calculating Avatar Disc Offset

```c
uint32_t disc_avatar_offset(uint32_t avatar_index, uint32_t block_size) {
    return avatar_list[avatar_index] * block_size;
}
```

## Linked Memory File Entry Structure

Used for special file entries in linked memory structures.

### Memory Layout

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0x00 | 4 | `fingerprint` | `uint32_t` | Entry type fingerprint |
| 0x04 | 4 | `flink_offset` | `uint32_t` | Forward link offset |
| 0x08 | 4 | `blink_offset` | `uint32_t` | Backward link offset |
| 0x0C | 4 | `block_count` | `uint32_t` | Number of blocks |
| 0x10 | 4 | `header_block_count` | `uint32_t` | Header blocks |
| 0x14 | 4 | `byte_count` | `uint32_t` | File size in bytes |
| 0x18 | 4 | `unique_identifier` | `uint32_t` | Unique ID |
| 0x1C | 4 | `type` | `uint32_t` | Entry type |
| 0x20 | 32 | `filename` | `char[32]` | Filename |

### Fingerprint Values

| Value | Name | Description |
|-------|------|-------------|
| `0xBE4F32A6` | `FINGERPRINT_FILEBLOCK` | File block entry |
| `0x7AA565BD` | `FINGERPRINT_FREEBLOCK` | Free block entry |
| `0x855A02B6` | `FINGERPRINT_ANCHORBLOCK` | Anchor block entry |

## Directory Traversal

### Reading a Directory

1. Seek to the root directory block (from `DiscLabel.root_directory_avatar_list[0]`)
2. Read `DirectoryHeader`
3. Read `DirectoryRecord` entries sequentially
4. For each entry:
   - Check flags to determine if file or directory
   - For directories, recursively traverse using `avatar_list[0]`
   - For files, access data at `avatar_list[0] * block_size`
5. Continue until `DR_FLAG_LAST_IN_DIR` is set
6. If `DR_FLAG_LAST_IN_BLOCK` is not set and `next_block != -1`, continue to next block

### Path Resolution

OperaFS paths use forward slashes:
```
system/kernel/boot_code
bannerscreen
signatures
```

Path resolution algorithm:
1. Start at root directory
2. Split path into components
3. For each component:
   - Scan current directory for matching filename
   - If found and more components, descend into subdirectory
   - If found and last component, return the record
   - If not found, return error

## File Data Access

### Reading File Data

1. Locate file via directory traversal
2. Get first avatar block from `avatar_list[0]`
3. Calculate disc offset: `block_offset = avatar_list[0] * block_size`
4. Read `byte_count` bytes starting at that offset

### Handling Multi-Block Files

For files spanning multiple blocks:
1. Start at `avatar_list[0]`
2. Read `block_size` bytes
3. Continue reading consecutive blocks
4. Total blocks needed: `block_count` or `(byte_count + block_size - 1) / block_size`

## Filename Handling

### Naming Rules
- Maximum length: 31 characters (32 bytes including null terminator)
- Case-insensitive on 3DO hardware
- Valid characters: A-Z, a-z, 0-9, underscore, period, hyphen
- No path separators in filename (only in full path)

### Case Handling
When comparing filenames:
```c
// Convert to lowercase for comparison
std::string lc_filename = to_lower(record.filename);
if (lc_filename == to_lower(search_name)) {
    // Match found
}
```

## Special Files

### System Files

OperaFS expects certain system files for bootable discs:

| Path | Purpose | ROM Tag Type |
|------|---------|--------------|
| `system/kernel/boot_code` | Boot executable | `RSA_NEWKNEWNEWGNUBOOT` |
| `system/kernel/os_code` | Operating system | `RSA_OS` |
| `system/kernel/misc_code` | Miscellaneous code | `RSA_MISCCODE` |
| `bannerscreen` | Splash screen image | `RSA_APPSPLASH` |
| `signatures` | Block signature database | `RSA_SIGNATURE_BLOCK` |

## Free Space Management

Free space is tracked through:
1. `DirectoryHeader.first_free_byte` - offset to unused space in directory block
2. `FINGERPRINT_FREEBLOCK` entries in linked memory

## Example: Parsing Root Directory

```c
// Open disc and seek to root directory
uint32_t root_block = disc_label.root_directory_avatar_list[0];
seek_to_block(root_block);

// Read directory header
DirectoryHeader header;
read(&header, sizeof(header));

// Read entries
while (true) {
    DirectoryRecord record;
    read(&record.flags, 4);
    read(&record.unique_identifier, 4);
    read(&record.type, 4);
    read(&record.block_size, 4);
    read(&record.byte_count, 4);
    read(&record.block_count, 4);
    read(&record.burst, 4);
    read(&record.gap, 4);
    read(&record.filename, 32);
    read(&record.last_avatar_index, 4);
    
    // Read avatar list
    for (int i = 0; i <= record.last_avatar_index; i++) {
        uint32_t avatar;
        read(&avatar, 4);
        record.avatar_list.push_back(avatar);
    }
    
    // Process record...
    
    if (record.flags & DR_FLAG_LAST_IN_DIR)
        break;
}
```

## Byte Order

All multi-byte integers are big-endian:
- `uint32_t` values require byte-swapping on little-endian systems
- Text fields (`filename`, etc.) are byte-order independent
