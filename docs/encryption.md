# 3DO Decryption Algorithm

## Overview

3DO uses a simple XOR-plus-nibble-swap obfuscation algorithm to encrypt certain boot-time files. This is primarily associated with CD dipir boot code and is not a cryptographically strong encryption scheme.

## Algorithm Details

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DECRYPT_MASK_1` | `0xF0F0F0F0` | High nibble extraction mask |
| `DECRYPT_MASK_2` | `0x0F0F0F0F` | Low nibble extraction mask |

### Key Material

The decryption uses a single 64-byte key derived from the 3DO RSA modulus:

#### BOOT_CODE_CRYPT_KEY (64 bytes)

The 3DO key modulus in raw binary form (64 bytes, no leading zero):
```
00 B1 94 62 B0 0D 8D 6E 1E C9 09 AB 38 5E 06 FE
03 4B FD 28 2E 9F FD C5 84 83 8C 15 F1 25 93 DD
1E 3A 8B 56 26 F1 B9 D0 ED 0C 38 4E F6 C5 D1 45
12 BD 72 DD B8 5B 44 08 0E 04 72 C0 3D 0A FC 4C
```

**Note:** The original Portfolio OS `portfolio_os/src/dipir/rsadipir.c` code
also references `PUBLIC_SIGNATURES_ARRAY` (a 432-byte ASCII credits message),
but that pointer is overwritten before the first dereference and every 16 words
thereafter. In practice, only `BOOT_CODE_CRYPT_KEY` (equivalent to
`PUBLIC_THDOKEY_MOD` without the leading zero) is used.

## Decryption Algorithm

### Process

The algorithm operates on 32-bit words (4 bytes at a time) using a single 64-byte repeating key:

```c
static uint32_t key_word(uint64_t word_offset) {
    uint32_t key;
    uint64_t key_offset = (word_offset % 16) * sizeof(uint32_t);
    memcpy(&key, &BOOT_CODE_CRYPT_KEY[key_offset], sizeof(key));
    return key;
}

void decrypt(void *buffer, uint64_t buffer_length) {
    uint32_t *p = (uint32_t*)buffer;
    uint32_t mask_1 = DECRYPT_MASK_1;  // 0xF0F0F0F0
    uint32_t mask_2 = DECRYPT_MASK_2;  // 0x0F0F0F0F
    
    for (uint64_t i = 0; i < buffer_length / 4; i++) {
        uint32_t key = key_word(i);
        uint32_t xored = *p ^ key;
        uint32_t part_a = (xored & mask_1) >> 4;  // Extract high nibbles, shift right
        uint32_t part_b = (xored & mask_2) << 4;  // Extract low nibbles, shift left
        *p = part_a | part_b;  // Combine
        
        p++;
    }
}
```

### Step-by-Step Explanation

1. **Initialize pointer:**
   - `p` points to the current 32-bit word in the buffer

2. **Key selection:**
   - Each word uses `BOOT_CODE_CRYPT_KEY[(i % 16) * 4]` as the 4-byte XOR key
   - The 64-byte key repeats every 16 words (64 bytes)

3. **XOR with key:**
   - XOR the current buffer word with the current key word

4. **Nibble swap:**
   - Extract high nibbles (bits 7, 15, 23, 31) using mask `0xF0F0F0F0`
   - Shift right by 4 bits
   - Extract low nibbles (bits 3, 11, 19, 27) using mask `0x0F0F0F0F`
   - Shift left by 4 bits
   - Combine the two parts

5. **Advance:**
   - Move to next word in buffer

### Visualization

```
Input byte:  0xAB
Key byte:    0x12
XOR result:  0xB9

Before nibble swap:
  0xB9 = 1011 1001
  & 0xF0 = 1011 0000 (high nibble)
  & 0x0F = 0000 1001 (low nibble)

After nibble swap:
  high >> 4 = 0000 1011
  low << 4  = 1001 0000
  Combined  = 1001 1011 = 0x9B
```

## Encryption

Encryption uses the same key schedule, but it is not identical to decryption. Decryption XORs each word with the key word and then swaps nibbles. The inverse operation swaps nibbles first and then XORs with the key word:

```c
decrypted = swap_nibbles(encrypted ^ key);
encrypted = swap_nibbles(decrypted) ^ key;
```

Applying the decryption routine twice does not generally return the original data.

## Usage in 3DO

### File Decryption Command

The `decrypt-file` subcmd applies this algorithm to entire files:
This is primarily the CD-DIPIR boot payload path and is the CLI equivalent of
`DecryptBlock()` in Portfolio OS `src/dipir/cdipir.c`.

```cpp
void decrypt_file(const std::filesystem::path &filepath) {
    // Read entire file
    std::vector<char> data = read_file(filepath);
    
    // Align size down to 4-byte boundary
    uint64_t aligned_size = TDO::boot_code_crypto_aligned_size(data.size());
    
    // Decrypt in place
    TDO::decrypt_boot_code_range(data.data(), aligned_size);
    
    // Write back
    write_file(filepath, data);
}
```

### CD-DIPIR Workflow

Typical boot payload workflow when auditing a 3DO CD-DIPIR image:

```sh
3dt unpack game.iso ./game.unpacked
3dt decrypt-file ./game.unpacked/system/kernel/boot_code   # inspect/patch
3dt encrypt-file ./game.unpacked/system/kernel/boot_code   # restore obfuscated payload
```

### File Encryption Command

The new `encrypt-file` subcmd is the inverse transform and is intended to
return files to the obfuscated form after inspection or patching. In practice,
this is primarily used for the boot-file path (`System/Kernel/boot_code`).

```cpp
void encrypt_file(const std::filesystem::path &filepath) {
    // Read entire file
    std::vector<char> data = read_file(filepath);

    // Align size down to 4-byte boundary
    uint64_t aligned_size = TDO::boot_code_crypto_aligned_size(data.size());

    // Encrypt in place
    TDO::encrypt_boot_code_range(data.data(), aligned_size);

    // Write back
    write_file(filepath, data);
}
```

### Files Typically Encrypted

This scheme is mainly a boot-time dipir mechanism. Most signed files on a 3DO disc are RSA-signed but not encrypted with this transform.

| Filesystem path | ROM tag(s) | Notes |
|-----------------|------------|-------|
| `System/Kernel/boot_code` | `RSA_NEWNEWBOOT`, `RSA_NEWNEWGNUBOOT`, `RSA_NEWKNEWNEWGNUBOOT` | The usual CD dipir bootstrap file. The ROM tag points to this payload; dipir verifies it, decrypts it with `DecryptBlock()`, then verifies the decrypted payload again. |
| `System/Kernel/_boot_code` | Not standardized | Seen on some development, SDK, and homebrew-style images as an alternate boot-code blob. Treat this as a candidate only; the active encrypted payload is the file referenced by the boot ROM tag. |
| Legacy boot files | `RSA_BOOT`, `RSA_NEWBOOT` | Older boot-code formats may use related signing or key schemes. Not every legacy boot tag uses this exact nibble-swap transform. |

Path case varies between discs. Portfolio OS lookup is case-insensitive for OperaFS entries, so `system/kernel/boot_code` and `System/Kernel/boot_code` should be treated as the same logical path.

### Files Usually Not Encrypted

The following files are commonly signed or digest-covered, but are not normally decrypted with this algorithm:

| Filesystem path | ROM tag(s) | Notes |
|-----------------|------------|-------|
| `System/Kernel/os_code` | `RSA_OS` | RSA-signed OS component. |
| `System/Kernel/misc_code` | `RSA_MISCCODE`, `RSA_OLD_MISCCODE` | RSA-signed misc-code component. |
| `BannerScreen` | `RSA_APPSPLASH` | APP-key-signed splash image when present. |
| `signatures` | `RSA_SIGNATURE_BLOCK` | APP-key-signed MD5 digest table for image blocks. |
| `LaunchMe`, `AppStartup` | `RSA_BLOCKS_ALWAYS` or no ROM tag | Application startup files may be covered by signatures or digest checks, but are not typical `DecryptBlock()` targets. |

### When Decryption is Used

1. **CD dipir boot code** - Retail and late-development discs commonly protect `System/Kernel/boot_code` through the boot ROM tag.
2. **Legacy boot variants** - Older boot tags such as `RSA_NEWNEWBOOT` and `RSA_NEWNEWGNUBOOT` describe earlier protected boot-code formats.
3. **Developer artifacts** - Some disc images contain extra boot-code copies, such as `_boot_code`, that may need manual inspection before decrypting.

## Security Analysis

### Weaknesses

1. **Not cryptographically secure**
   - Simple XOR cipher with fixed key
   - Known plaintext attack is trivial

2. **Fixed key material**
   - Both key arrays are public and constant
   - No key derivation or salting

3. **Short key period**
   - Key repeats every 64 bytes
   - Pattern analysis is straightforward

4. **Deterministic**
   - Same input always produces same output
   - No initialization vector

### Purpose

The encryption appears designed for:
- **Obfuscation** rather than security
- **Preventing casual inspection** of sensitive files
- **Compatibility** with legacy 3DO tools

### Modern Perspective

By modern standards, this encryption provides no meaningful security:
- Key is publicly known
- Algorithm is reversible and deterministic
- No authentication or integrity verification

## Implementation Notes

### Alignment Requirements

- Input buffer should be 4-byte aligned for optimal performance
- Buffer length should be a multiple of 4 bytes
- Trailing bytes (if length % 4 != 0) are not processed

### Endianness

- The algorithm operates on 32-bit words in native byte order
- On little-endian systems, byte order within words is reversed
- Results are consistent across platforms for the same input

### Memory Safety

```c
// Ensure buffer is large enough
assert(buffer_length >= 4);
assert(buffer != NULL);

// Process complete 32-bit words only
uint64_t words = buffer_length / 4;
```

## Example Implementation

```c
#include <stdint.h>
#include <string.h>

static const uint8_t BOOT_CODE_CRYPT_KEY[64] = {
    0x00, 0xB1, 0x94, 0x62, 0xB0, 0x0D, 0x8D, 0x6E,
    0x1E, 0xC9, 0x09, 0xAB, 0x38, 0x5E, 0x06, 0xFE,
    0x03, 0x4B, 0xFD, 0x28, 0x2E, 0x9F, 0xFD, 0xC5,
    0x84, 0x83, 0x8C, 0x15, 0xF1, 0x25, 0x93, 0xDD,
    0x1E, 0x3A, 0x8B, 0x56, 0x26, 0xF1, 0xB9, 0xD0,
    0xED, 0x0C, 0x38, 0x4E, 0xF6, 0xC5, 0xD1, 0x45,
    0x12, 0xBD, 0x72, 0xDD, 0xB8, 0x5B, 0x44, 0x08,
    0x0E, 0x04, 0x72, 0xC0, 0x3D, 0x0A, 0xFC, 0x4C,
};

uint32_t key_word(uint64_t word_offset) {
    uint32_t key;
    uint64_t key_offset = (word_offset % 16) * sizeof(uint32_t);
    memcpy(&key, &BOOT_CODE_CRYPT_KEY[key_offset], sizeof(key));
    return key;
}

void tdo_decrypt(void *buffer, uint64_t length) {
    uint32_t *p = (uint32_t*)buffer;

    for (uint64_t i = 0; i < length / 4; i++) {
        uint32_t xored = *p ^ key_word(i);
        uint32_t part_a = (xored & 0xF0F0F0F0) >> 4;
        uint32_t part_b = (xored & 0x0F0F0F0F) << 4;
        *p++ = part_a | part_b;
    }
}

void tdo_encrypt(void *buffer, uint64_t length) {
    uint32_t *p = (uint32_t*)buffer;

    for (uint64_t i = 0; i < length / 4; i++) {
        uint32_t word = *p;
        uint32_t swapped = ((word & 0xF0F0F0F0) >> 4) |
                           ((word & 0x0F0F0F0F) << 4);
        *p++ = swapped ^ key_word(i);
    }
}
```

## Historical Context

This algorithm originates from the Portfolio OS source code, specifically
`src/dipir/cdipir.c` (CD-DIPIR `DecryptBlock()`). It was used internally by
3DO developers for boot-time obfuscation in production and validation workflows.
