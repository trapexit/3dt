# 3DO Disc Format Technical Reference

This directory contains comprehensive technical documentation for the 3DO Interactive Multiplayer disc format and related technologies.

## Documents

| Document | Description |
|----------|-------------|
| [disc-format.md](disc-format.md) | Physical disc structure, sector formats, and DiscLabel |
| [filesystem.md](filesystem.md) | OperaFS filesystem: directories, files, and avatars |
| [romtags.md](romtags.md) | ROM Tag metadata structures and component types |
| [signing.md](signing.md) | RSA-512 signing algorithm, keys, and verification |
| [encryption.md](encryption.md) | XOR-based file obfuscation algorithm |

## Quick Reference

### Key Constants

| Constant | Value |
|----------|-------|
| Block Size | 2048 bytes |
| Max Filename | 32 characters |
| Avatar Count | 8 (indices 0-7) |
| RSA Key Size | 512 bits |
| Signature Size | 64 bytes |
| Hash Algorithm | MD5 |

### Data Structures

| Structure | Size | Location |
|-----------|------|----------|
| DiscLabel | 132 bytes | Block 0 |
| DirectoryHeader | 20 bytes | Start of each directory block |
| DirectoryRecord | 72+ bytes | After DirectoryHeader |
| ROMTag | 24 bytes | Block after DiscLabel (typically block 1) |
| RSA Signature | 64 bytes | Appended to signed data |

### Byte Order

All multi-byte integers are **big-endian**.

## File Locations

Standard 3DO disc file paths:

```
/
├── signatures                      # Block MD5 digests
├── bannerscreen                    # Splash screen image
├── rom_tags                        # Synthetic ROM tag table file
├── launchme                        # Application startup file
└── system/
    └── kernel/
        ├── boot_code               # Boot executable
        ├── os_code                 # Operating system
        └── misc_code               # Miscellaneous code
```

## Signing Overview

1. Pad image to 32KB boundary
2. Generate ROM tags for special files
3. Sign bannerscreen with APP key
4. Generate and sign signatures file
5. Sign DiscLabel + ROMTags + boot_code with APP key
6. OS code and misc_code signed with 3DO key

## Tools

The `3dt` command-line tool provides:

```sh
3dt list <disc.iso>              # List disc contents
3dt info <disc.iso>              # Display disc information
3dt identify <disc.iso>          # Identify disc by database
3dt unpack <disc.iso> <dir>      # Extract disc contents
3dt pack <dir> <disc.iso>        # Build disc image from directory
3dt rename <disc.iso> <name>     # Rename disc volume
3dt romtags <disc.iso>           # Display ROM tags
3dt verify <disc.iso>            # Verify RSA signatures
3dt sign <disc.iso>              # Sign disc image
3dt sign-file <file>             # Sign individual file
3dt decrypt-file <file>          # Decrypt obfuscated file
3dt to-iso <disc.bin> <out.iso>  # Convert BIN to ISO
```

## References

- 3DO Portfolio OS source code (internal)
- Opera Filesystem specification
- RSA PKCS#1 v1.5 standard
- RFC 1321 (MD5)
