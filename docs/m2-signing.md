# M2 Disc Signing

## Overview

M2 discs use the same broad MD5 + RSA PKCS#1 v1.5 model as Opera discs, but the signing layout is different enough that it should be treated as a separate workflow.

This document is based on the Portfolio OS M2 source tree at `~/dev/portfolio_os_m2/ws_root/src/`.

## Key Differences From Opera

| Area | Opera / M1 | M2 |
|------|------------|----|
| Label | `DiscLabel` | `ExtVolumeLabel` |
| ROMTag count | Null-terminated table | `dl_NumRomTags` in label |
| Main signature size | 64 bytes | 128 bytes |
| Main key | 3DO/App 64-byte keys | M2 128-byte key |
| ROMTag table max | 2048-byte block convention | Up to 8192 bytes |
| File signatures | Final 64 bytes | Final 128 bytes |

Relevant source:
- `includes/file/discdata.h`: `ExtVolumeLabel`, `VF_M2`, `VF_M2ONLY`, `VF_BLESSED`
- `includes/dipir/rom.h`: `RomTag`, M2 ROMTag types
- `others/dipir/dipir.h`: `KEY_128`, `SIG_128_LEN`, `MAX_ROMTAG_BLOCK_SIZE`

## Extended Volume Label

M2 discs use `ExtVolumeLabel`, which appends fields after the Opera label body:

| Field | Description |
|-------|-------------|
| `dl_NumRomTags` | Number of ROMTag entries in the table |
| `dl_ApplicationID` | Application identifier |
| `dl_Reserved[9]` | Reserved; must be zero |

The extended label totals 176 bytes (132 base + 44 extended).

The standard M2 layout tool creates labels with:
- `VF_M2 | VF_BLESSED` by default
- `VF_M2ONLY` set for M2-only media when requested
- `dl_NumRomTags` set explicitly by `setromtags`

Relevant source:
- `tools/layout/layout.c:336-356`
- `tools/layout/layout.c:424-452`
- `tools/layout/layout.tcl:1978-1987`
- `tools/layout/cdrom.tcl:37-50`

## ROMTag Table

For M2, the ROMTag table is an array of exactly `dl_NumRomTags` entries. It is not terminated by a null ROMTag when read as an M2 table.

The table starts at the block immediately after the extended label, rounded up to the next device block:

```c
romtag_block = ceil((label_block * block_size + sizeof(ExtVolumeLabel)) / block_size)
```

For normal 2048-byte CD images this is block `1`.

The M2 dipir rejects labels where `dl_NumRomTags` exceeds `MAX_ROMTAG_BLOCK_SIZE / sizeof(RomTag)`, with `MAX_ROMTAG_BLOCK_SIZE` set to 8192 bytes.

Relevant source:
- `includes/dipir/rom.h:37-45`
- `others/dipir/romtag.c:289-323`
- `others/dipir/dipir.h:92`

## M2 RSA Key

M2 signature checks use `KEY_128`, whose signature length is 128 bytes. `ReadSigned()` treats the last `KeyLen(key)` bytes as the signature and digests all preceding bytes.

Relevant source:
- `others/dipir/dipir.h:76-90`
- `others/dipir/dbuffer.c:164-200`
- `others/dipir/rsadipir.c:261-270`

The M2 source includes an `rsasign` tool with key names `opera` and `m2`. The tool defaults to `m2`. The key material in `tools/rsasign/keys.c` matches the demo key path accepted by dipir only when `DC_DEMOKEY` is enabled; do not assume it is a retail signing key.

Relevant source:
- `tools/rsasign/rsasign.c:16-19`
- `tools/rsasign/rsasign.c:421-461`
- `tools/rsasign/keys.c:129-137`
- `others/dipir/rsadipir.c:261-270`

## ROMTag Table Signature

The M2 ROMTag table signature verifies this byte stream:

```text
ExtVolumeLabel || RomTag[dl_NumRomTags]
```

The signature is checked with `KEY_128`.

Signature location depends on `VF_M2ONLY`:

| Volume flags | Signature location |
|--------------|--------------------|
| `VF_M2ONLY` set | Immediately after `RomTag[dl_NumRomTags]` |
| `VF_M2ONLY` clear | After `RomTag[dl_NumRomTags]`, then one null Opera ROMTag, then one 64-byte Opera signature |

In other words, hybrid M2/Opera media stores:

```text
RomTag[dl_NumRomTags]
Opera null RomTag
Opera 64-byte RTT signature
M2 128-byte RTT signature
```

The M2 signature does not include the Opera null ROMTag or Opera 64-byte signature in its digest. It covers only `ExtVolumeLabel` and the counted M2 ROMTags.

Relevant source:
- `others/dipir/romtag.c:309-319`
- `others/dipir/m2dipir.c:581-604`

## Signed M2 Assets

M2 assets referenced by ROMTags are signed as whole files where the final 128 bytes are the RSA signature. The `rt_Size` includes those 128 signature bytes.

Digest input:

```text
asset_bytes[0 : rt_Size - 128]
```

Signature bytes:

```text
asset_bytes[rt_Size - 128 : rt_Size]
```

The block address used by dipir is:

```c
start_block = fd_RomTagBlock + rt_Offset
```

This is different from the common 3dt/Opera convention that displays an asset block as `romtag.offset + 1` when the table is at block `1`.

Relevant source:
- `others/dipir/dbuffer.c:164-200`
- `others/dipir/diplib.verify.c:18-28`
- `others/dipir/diplib.banner.c:19-41`
- `others/dipir/dipir.cd.c:138-164`

## Device Dipir Signature

`RSA_M2_DEVDIPIR` has special coverage when loaded from a device. The signature at the end of the device dipir covers:

```text
ExtVolumeLabel || RomTag[dl_NumRomTags] || device_dipir_body_without_signature
```

The trailing signature is still 128 bytes and is checked with `KEY_128`.

Relevant source:
- `others/dipir/m2dipir.c:475-491`

## Assets Checked During CD Validation

The CD device dipir checks M2 assets with `KEY_128`:

| ROMTag | Purpose |
|--------|---------|
| `RSA_M2_APPBANNER` | Banner read/display and validation |
| `RSA_M2_OS` | OS read, validation, relocation, and boot |

When the current OS remains running, the dipir still verifies the banner, media, and on-disc M2 OS.

Relevant source:
- `others/dipir/dipir.cd.c:116-133`
- `others/dipir/dipir.cd.c:138-176`

Other M2 ROMTag types include `RSA_M2_MISCCODE`, `RSA_M2_DRIVER`, `RSA_M2_DEVDIPIR`, `RSA_M2_APP_KEYS`, and `RSA_M2_ICON`. The source comments describe `RSA_M2_APP_KEYS` as `65 followed by 129 byte APP key`, but the searched source does not show it being used by the CD validation path.

Relevant source:
- `includes/dipir/rom.h:90-120`

## Loader File Signatures

The M2 loader also verifies ordinary loader files using the public dipir RSA interface. It reads all file bytes except the final `RSA_KEY_SIZE` bytes into the digest, then verifies the final signature. `RSA_KEY_SIZE` is 128.

Relevant source:
- `includes/dipir/dipirpub.h:35`
- `libs/loader/fileio.c:25-57`
- `libs/loader/fileio.c:103-107`
- `libs/loader/fileio.c:205-234`

## Implementation Notes For 3dt

M2 signing should be implemented separately from the current Opera signing path.

Required behavior:
- Write `ExtVolumeLabel`, not `DiscLabel`, when `VF_M2` is set.
- Set `dl_NumRomTags` to the exact M2 ROMTag count.
- Generate counted M2 ROMTags without relying on a null terminator.
- Append 128-byte signatures to signed M2 assets and include those bytes in `rt_Size`.
- Sign M2 assets with the 128-byte M2 key path, not the 64-byte 3DO/App keys.
- Sign the M2 RTT digest over `ExtVolumeLabel || counted ROMTags`.
- Place the M2 RTT signature immediately after the counted table for `VF_M2ONLY` media.
- For hybrid media, reserve space for `null Opera ROMTag || Opera 64-byte RTT signature || M2 128-byte RTT signature` after the counted table.
- Validate `RSA_M2_DEVDIPIR` with its special `label + table + body` coverage if supporting device dipirs.

Open questions before implementing retail M2 signing:
- Which private 128-byte key should be used for target hardware.
- Whether hybrid M2/Opera discs produced by 3dt should also generate a meaningful Opera 64-byte RTT signature.
- Whether `RSA_M2_APP_KEYS` needs support for any target samples or hardware configuration.
