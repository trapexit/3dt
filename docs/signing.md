# 3DO RSA Signing

## Overview

3DO discs use RSA-512 digital signatures to verify the authenticity and integrity of disc contents. This document describes the signing algorithm, key structure, and verification process.

## RSA Parameters

| Parameter | Value |
|-----------|-------|
| Algorithm | RSA |
| Key Size | 512 bits |
| Signature Size | 64 bytes |
| Hash Algorithm | MD5 |
| Public Exponent (e) | 65537 (0x10001) |

## Signature Types

### rsa512_sig_t
```c
typedef unsigned char rsa512_sig_t[64];  // 512 bits = 64 bytes
```

## Keys

### Key Names

| Name | Constant | Description |
|------|----------|-------------|
| `"3do"` | `TDO_KEY_3DO` | 3DO system key |
| `"app"` | `TDO_KEY_APP` | Application/publisher key |

### RSA Key Components

Each key has three components:
- **n**: Modulus (public, 512 bits)
- **d**: Private exponent (secret, 512 bits)
- **e**: Public exponent (65537)

### 3DO Key (System Key)

**Modulus (n):**
```
B19462B00D8D6E1EC909AB385E06FE034BFD282E9FFDC584838C15F12593DD1E
3A8B5626F1B9D0ED0C384EF6C5D14512BD72DDB85B44080E0472C03D0AFC4C97
```

**Private Exponent (d):**
```
42F7CD9BCD109805BE150A60107D9C8F8BB9A5CCA78361588EEF665AF1ABE887
DBC2593D0868F364A93C8CB8CC6F4BCC6A3DE57E04B17AC52F2649939C453F61
```

### APP Key (Application Key)

**Modulus (n):**
```
BC0B199086C7F26CBC9D50F404944DB4789FCBFCF7AD8DBC2120898ABEAAF311
EEA20229035608841FA41073ABBD5D37500C60B53BFB46605740381B72C9DB71
```

**Private Exponent (d):**
```
18B2207E61A51ACA7B0EF215CA102C105A9329F824130FFD38208CCFC2F0B291
5B8AD1E7772334381737D232B183C869C34940BC8769C97E18D7B0E78C492991
```

## Message Formatting

Before signing, the MD5 digest is formatted into a PKCS#1 v1.5 compatible message.

### Message Prefix
```
1ffffffffffffffffffffffffffffffffffffffffffffffffffffff003020300c06082a864886f70d020505000410
```

This 70-character hex string (35 bytes) is prepended to the MD5 digest.

### Full Message Construction
```
message = prefix || MD5_digest_hex
```

**Example:**
```
Prefix:  1ffffffffffffffffffffffffffffffffffffffffffffffffffffff003020300c06082a864886f70d020505000410
MD5:     93A6D77F3DC4DF73D7F819598DE97DDB
Message: 1ffffffffffffffffffffffffffffffffffffffffffffffffffffff003020300c06082a864886f70d02050500041093A6D77F3DC4DF73D7F819598DE97DDB
```

### Prefix Breakdown

| Bytes | Value | Meaning |
|-------|-------|---------|
| 1 | `0x1F` | PKCS#1 block type 1 (signature) |
| 15 | `FF...FF` | Padding bytes |
| 1 | `0x00` | Separator |
| 2 | `0x30 0x20` | SEQUENCE (32 bytes) |
| 2 | `0x30 0x0C` | Nested SEQUENCE (12 bytes) |
| 2 | `0x06 0x08` | OID tag (8 bytes) |
| 8 | `2A864886F70D0205` | MD5 OID (1.2.840.113549.2.5) |
| 2 | `0x05 0x00` | NULL |
| 2 | `0x04 0x10` | OCTET STRING (16 bytes) |
| 16 | MD5 digest | The actual hash |

## Signing Algorithm

### RSA Signature Generation

```
signature = m^d mod n
```

Where:
- `m` = formatted message (as big integer)
- `d` = private exponent
- `n` = modulus

### Implementation

```c
void tdo_rsa_sign(const char *key_name, 
                  const md5_digest_t digest, 
                  rsa512_sig_t signature) {
    // Get key components
    BIGD n = get_modulus(key_name);      // n
    BIGD d = get_private_exponent(key_name);  // d
    BIGD m = format_message(digest);     // m = prefix || digest
    
    // Compute signature: s = m^d mod n
    BIGD s = bdNew();
    bdModExp(s, m, d, n);  // s = m^d mod n
    
    // Convert to bytes
    bdConvToOctets(s, signature, 64);
    
    // Cleanup
    bdFree(&s);
    bdFree(&m);
    bdFree(&d);
    bdFree(&n);
}
```

## Signed Components

### Components Signed with APP Key

| Component | Data Signed | Signature Location |
|-----------|-------------|-------------------|
| DiscLabel + ROMTags + BootCode | Concatenated data | After ROM tags |
| Signatures File | All MD5 digests | End of file |
| BannerScreen | Image data | End of file |

### Components Signed with 3DO Key

| Component | Data Signed | Signature Location |
|-----------|-------------|-------------------|
| OS Code | `system/kernel/os_code` | End of file |
| Misc Code | `system/kernel/misc_code` | End of file |
| Boot Code (inner decrypted payload) | Post-decryption inner boot code | End of encrypted `boot_code` file |

## Signature Verification

### Verification Algorithm

```
m' = s^e mod n
```

Where:
- `s` = signature (as big integer)
- `e` = public exponent (65537)
- `n` = modulus

The result `m'` should match the formatted message.

### Implementation

```c
bool verify_signature(const char *key_name,
                      const md5_digest_t digest,
                      const rsa512_sig_t signature) {
    // Get public key components
    BIGD n = get_modulus(key_name);
    BIGD e = bdNew(); bdConvFromHex(e, "10001");
    
    // Convert signature to big integer
    BIGD s = bdNew(); bdConvFromOctets(s, signature, 64);
    
    // Compute: m' = s^e mod n
    BIGD m_prime = bdNew();
    bdModExp(m_prime, s, e, n);
    
    // Compute expected message
    BIGD m_expected = format_message(digest);
    
    // Compare
    bool valid = (bdCompare(m_prime, m_expected) == 0);
    
    // Cleanup
    bdFree(&m_expected);
    bdFree(&m_prime);
    bdFree(&s);
    bdFree(&e);
    bdFree(&n);
    
    return valid;
}
```

## Signing a Disc

### Process Overview

1. **Pad image to 32KB boundary**
   - Image size must be multiple of 32768 bytes

2. **Update disc label**
   - Set `volume_block_count` to new total blocks

3. **Add 3dt mark** (optional)
   - Write signature marker at offset 0x100

4. **Generate and write ROM tags**
   - Create ROMTag for each special file
   - Write terminator
   - Validate all required files exist

5. **Sign BannerScreen**
   - Read bannerscreen data
   - Compute MD5
   - Sign with APP key
   - Append signature

6. **Generate `RSA_BILLSTUFF` ROM tag**
   - Create ROM tag for Bill Duvall component

7. **Generate signatures file**
   - Compute MD5 for each 32KB block
   - Build signatures file
   - Sign digest list with APP key
   - Append signature

8. **Sign DiscLabel + ROMTags + BootCode**
   - Concatenate: DiscLabel + ROMTags + boot_code (encrypted outer payload)
   - Compute MD5
   - Sign with APP key
   - Write signature after ROM tags

9. **Sign boot code inner payload with 3DO key**
   - Decrypt boot_code (remove XOR obfuscation)
   - Compute MD5 of decrypted inner payload
   - Sign with 3DO key
   - Append signature to encrypted boot_code file

### Signatures File Format

```
+------------------+
| MD5 Digest 1     | 16 bytes
| MD5 Digest 2     | 16 bytes
| ...              |
| MD5 Digest N     | 16 bytes
+------------------+
| RSA Signature    | 64 bytes
+------------------+
```

### Digest Count Calculation

```c
uint64_t num_digests = (volume_block_count * volume_block_size) / 32768;
```

Each digest covers 32KB (16 blocks of 2048 bytes):
- Physical block size: 2048 bytes
- Logical block size: 32768 bytes (16 physical blocks)
- Each MD5 covers one 32KB logical block

### BannerScreen Sizes

| Type | Size (bytes) | With Signature |
|------|--------------|----------------|
| NTSC | 153,624 | 153,688 |
| PAL | 202,752 | 202,816 |

Calculation:
- NTSC: (320 × 240 × 2) + 24 = 153,624
- PAL: (352 × 288 × 2) + 24 = 202,752

## MD5 Implementation

### MD5 Digest Structure

```c
typedef uint8_t md5_digest_t[16];
```

### MD5 API

```c
void md5_init(md5_ctx_t *ctx);
void md5_update(md5_ctx_t *ctx, const void *data, size_t size);
void md5_finalize(md5_ctx_t *ctx, md5_digest_t digest);
void md5_calc(const void *data, size_t size, md5_digest_t digest);
```

### Example: Computing File MD5

```c
void compute_file_md5(DevStream &stream, uint64_t block, 
                      uint64_t size, md5_digest_t digest) {
    std::vector<char> data;
    stream.read_data_bytes_from_block(data, block, size);
    md5_calc(data.data(), data.size(), digest);
}
```

## Cross-Application Signature

The "cross-app" signature is stored immediately after the ROM tags:

```c
void get_cross_app_sig(DevStream &stream, rsa512_sig_t sig) {
    stream.data_block_seek(stream.romtags_block());
    stream.data_byte_skip(stream.romtags_size_in_bytes());
    stream.read((char*)sig, 64);
}
```

This signature covers:
1. DiscLabel (132 bytes)
2. All ROM tags (24 bytes each + terminator)
3. Boot code (from RSA_NEWKNEWNEWGNUBOOT)

## Signature Validation During Boot

The 3DO boot process:

1. **Read DiscLabel** - Find and validate disc label
2. **Read ROM Tags** - Locate special components
3. **Verify cross-app signature** - Validates DiscLabel + ROMTags + boot_code
4. **Load boot_code** - Verify with 3DO key signature
5. **Load OS** - Verify with 3DO key signature
6. **Validate signatures file** - Spot-check block digests
7. **Boot application**

### Signature File Checking

The `type_specific` field in RSA_SIGNATURE_BLOCK controls how many block digests are checked:

- `MAX_DIGEST_CHECKS` = 128 (maximum)
- Default: 15 digests checked
- Setting to 0: disables checking
- Setting to 1: uses default (15)
- Setting ≥ 128: uses 127

## Security Notes

### Key Security

- The **private exponent (d)** must be kept secret
- The **modulus (n)** and **public exponent (e)** can be public
- Keys are 512-bit RSA, which is considered weak by modern standards
- 3DO uses fixed, known keys for retail discs

### Signature Storage

- Signatures are appended to the end of signed data
- File signatures replace any existing signature
- The cross-app signature is stored at a fixed location

### Known Boot Code MD5

The standard 3DO boot code has a known MD5:
```c
md5_digest_t MD5_DIGEST_BOOT_CODE = {
    0x93, 0xA6, 0xD7, 0x7F, 0x3D, 0xC4, 0xDF, 0x73,
    0xD7, 0xF8, 0x19, 0x59, 0x8D, 0xE9, 0x7D, 0xDB
};
```

Size: 5996 bytes (some tools incorrectly pad to 8192 bytes).
