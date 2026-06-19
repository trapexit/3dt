# 3dt: 3DO Disc Tool

## Features

* list OperaFS content
* list lowlevel disc/OperaFS details
* list ROM tags
* identify discs and ROMs
* rename image based on identification
* unpack disc and ROM OperaFS contents
* pack directories into 3DO disc images
* repack existing images and compact free space
* convert raw disc images to .iso
* supports reading CD-ROM Mode 1 images and 2048 byte/sector ISOs
* sign and re-sign 3DO images
* verify 3DO signatures
* encrypt or decrypt 3DO obfuscation payloads (primarily the boot file)


## Usage

### --help

```
3dt: 3DO Disc Tool (v1.2.0)


Usage: 3dt [OPTIONS] SUBCOMMANDS

OPTIONS:
  -h,     --help              Print this help message and exit
          --help-all

SUBCOMMANDS:
  version                     print 3dt version
  list                        list disc content
  info                        prints lowlevel info on disc
  identify                    attempt to identify disc image
  unpack                      unpack disc image
  pack                        pack a directory into a 3DO disc image
  repack                      repack a 3DO disc image compacting avatars and empty space
  rename                      rename disc image as identified
  to-iso                      convert a .bin or similar disc image to .iso
  romtags                     print out image romtags
  verify                      verify RSA sigs
  sign                        sign 3DO ISO for retail system use
  sign-file                   sign file with 3DO or APP key
  decrypt-file                decrypt CD-DIPIR boot payload (`src/dipir/cdipir.c`)
  encrypt-file                encrypt CD-DIPIR boot payload (`src/dipir/cdipir.c`)
```

Use `--help` on individual subcommands to get specific subcommand options.


### version

Print the 3dt version.

```
3dt version
```


### list

A `ls` or `dir` style listing of the disc's contents.

```
$ 3dt list ./Escape\ from\ Monster\ Manor\ \(USA\).iso | head
Flags      Size         ID Type Filename
-r-         169 0x3977524c      AppStartup
drf        2048 0x076da54f *dir Attic Level 2
-r-        4219 0x3e9ea1e3      Attic Level 2/FloorPlan
-r-      153048 0x015deb07 loaf Attic Level 2/atticwalls.loaf
-r-      227400 0x2f06c2f1 loaf Attic Level 2/powerups.loaf
drf        2048 0x03afa069 *dir Attic Simple
-r-        4158 0x11bfb126      Attic Simple/FloorPlan
-r-      153048 0x0003d024 loaf Attic Simple/atticwalls.loaf
-r-      226532 0x1e3c48de loaf Attic Simple/powerups.loaf
```

You can choose from a few output formats:

`list` supports `--format=default`, `--format=file-offsets`, and
`--format=block-offsets`. A second positional argument filters output to paths
with that prefix.

```
$ 3dt list --format=block-offsets SHADOW\ -\ War\ of\ Succession\ \(USA\).iso  | head
Flags      Size         ID Type  RecOffset     Avatar Filename
-r-      185064 0x273d2a36 anim 0x00012b0b 0x000024de 30fbl.anim
-r-        1784 0x27fd1ae1 cel  0x00012b0b 0x00002539 3dologo.cel
-r-       66932 0x00902938 FRAM 0x00012b0b 0x0000253a ALVIN.FRAMES
-r-        1100 0x1b218626 AA   0x00012b0b 0x0000255b ALVIN.HEAD.AA
-r-       27566 0x147cc12b MOVE 0x00012b0b 0x0000255c ALVIN.MOVES
-r-       36408 0x148b2e00 out  0x00012b0b 0x0000256a ALVIN.sprites.anim.out
-r-        5984 0x2d1e6785 out  0x00012b0b 0x0000257c ALVIN.sprites.data.out
-r-       82317 0x172bde1c FRAM 0x00012b0b 0x0000257f ANTHONY.FRAMES
-r-        1040 0x09ef093d AA   0x00012b0b 0x000025a8 ANTHONY.HEAD.AA
```


### info

Prints out the disc's "disc label", file count, and total data byte size.
Use `--format=human`, `--format=csv`, or `--format=cheader` to change output.

```
$ 3dt info ./PO\'ed.iso
 - record_type: 0x01
 - volume_sync_bytes: 0x5A
 - volume_structure_version: 0x01
 - volume_flags: 0x00
 - volume_commentary:
 - volume_identifier: CD-ROM
 - volume_unique_identifier: 0x198EEB79
 - volume_block_size: 2048
 - volume_block_count: 204800
 - root_unique_identifier: 0x1F2377CF
 - root_directory_block_count: 2
 - root_directory_block_size: 2048
 - root_directory_last_avatar_index: 6
 - root_directory_avatar_list:
   - 39473
   - 39475
   - 58515
   - 87772
   - 117029
   - 146286
   - 175543
 - file_count: 398
 - total_data_size: 80414372
```

### identify

The 3dt internal database includes a number of fields and metrics
found on a 3DO image. The "unique" identifiers are not necessarily
unique and as such the file count and total data size were used to
help in uniquly identifying images. If you come across an unknown
image (which isn't just random homebrew) please file a
[ticket](https://github.com/trapexit/3dt/issues) with the details from
`info` command.
Use `--format=human` or `--format=csv` to change output.

```
$ 3dt identify ./PO\'ed.iso
./PO'ed (USA, Europe).bin:
 - volume_unique_identifier: 0x198EEB79
 - volume_block_count: 204800
 - root_unique_identifier: 0x1F2377CF
 - file_count: 398
 - total_data_size: 80414372
 - matches:
   - PO'ed (USA, Europe)
```


### unpack

This will copy the files from the disc image to your local storage
device. It will create a directory based on the file name with
".unpacked" appended unless the `-o,--output` option is used to change
the directory it unpacks to.
Use `--format=human` or `--format=csv` to change logging output. A
`layout.json` file is written in the unpacked root by default; use `--layout`
to choose another path.


```
$ 3dt unpack ./PO\'ed.iso
-r-         334 0x398d91a4 0x20202020 (    ) AppStartup
-r-      153688 0x1e2c6ba5 0x20202020 (    ) BannerScreen
-r-        5406 0x0367a749 0x64656d6f (demo) POed.demo0
-r-        5406 0x278e1e30 0x64656d6f (demo) POed.demo1
-r-        5406 0x291a85ff 0x64656d6f (demo) POed.demo2
-r-        5406 0x0d68b8b8 0x64656d6f (demo) POed.demo3
-r-        5406 0x0169672d 0x64656d6f (demo) POed.demo4
-r-        5406 0x14193b38 0x64656d6f (demo) POed.demo5
-r-        5406 0x0035f235 0x64656d6f (demo) POed.demo6
-r-        5394 0x13a19901 0x64656d6f (demo) POed.demo7
...
```


### rename

If a match is found for the image it will rename the file based on the name in the internal database.
Multiple inputs are supported. Use `--take-first` when an image has multiple
matches and the first match should be used automatically.

```
$ 3dt rename ./POed.iso
./POed.iso: renamed to ./PO'ed (USA, Europe).iso
```


### to-iso

This, like many other tools, will take a raw CDROM image such as a .bin file from a .bin/.cue set and create a .iso file.
The output path is optional. Use `--force` to overwrite an existing output file.

```
$ 3dt to-iso ./PO\'ed\ \(USA\,\ Europe\).bin
./PO'ed (USA, Europe).iso: sector 205249 of 205249 written

$ 3dt identify PO\'ed\ \(USA\,\ Europe\).iso
PO'ed (USA, Europe).iso:
 - volume_unique_identifier: 0x198EEB79
 - volume_block_count: 204800
 - root_unique_identifier: 0x1F2377CF
 - file_count: 398
 - total_data_size: 80414372
 - matches:
   - PO'ed (USA, Europe)
```


### romtags

Print rom tags
Use `--format=human` or `--format=csv` to change output.

```
$ 3dt romtags Wolfenstein\ 3D\ \(USA\).iso
Wolfenstein 3D (USA).iso:
  - Offset:      1
    SubSysType:  0x0f
    Type:        0x0d (NEWKNEWNEWGNUBOOT)
    Version:     2
    Revision:    5
    Flags:       0
    TypeSpec:    0
    Size:        6448
  - Offset:      5
    SubSysType:  0x0f
    Type:        0x07 (OS)
    Version:     24
    Revision:    225
    Flags:       0
    TypeSpec:    0
    Size:        115520
  - Offset:      69
    SubSysType:  0x0f
    Type:        0x10 (MISCCODE)
    Version:     0
    Revision:    0
    Flags:       0
    TypeSpec:    0
    Size:        2908
  - Offset:      42713
    SubSysType:  0x0f
    Type:        0x02 (BLOCKS_ALWAYS)
    Version:     0
    Revision:    0
    Flags:       0
    TypeSpec:    0
    Size:        47
  - Offset:      42766
    SubSysType:  0x0f
    Type:        0x05 (SIGNATURE_BLOCK)
    Version:     0
    Revision:    0
    Flags:       0
    TypeSpec:    15
    Size:        57344
```


### pack

Build a 3DO disc image from a source tree.

```
3dt pack /path/to/source --output game.iso
```

If present, `layout.json` is used automatically; otherwise pass a custom path with `--layout`.

Common options:
- `--volume-label` / `--volume-commentary`: set label metadata
- `--volume-unique-id` / `--root-unique-id`: set identifiers
- `--dry-run`: validate layout and allocations without writing the image
- `--mark`: write a 3dt marker into the output image
- `--sign`: run signing as part of pack
- `--digest-check-count`: tune signature digest check policy
- `--no-banner-romtag` / `--no-rsa-appsplash`: disable RSA_APPSPLASH romtag generation
- `--billstuff-romtag`: enable RSA_BILLSTUFF romtag generation (off by
  default, really has no impact but was set in all original titles)

By default, `pack` derives the volume unique identifier from the CRC32 of
`BannerScreen` and the root unique identifier from the CRC32 of `LaunchMe`.
If `BannerScreen` is not present, the volume unique identifier is generated
randomly. Passing `--volume-unique-id` or `--root-unique-id` overrides the
derived value; passing `0` explicitly selects a random identifier.


### repack

Rebuild an image while compacting avatars and reclaiming free space.

```
3dt repack game.iso --output game-repacked.iso
```

When `--output` is omitted, repack writes to the input basename with a `.iso`
extension. For example, `3dt repack game.bin` writes `game.iso`.
Repack supports multiple input images. `--output` requires exactly one input.
Repack also supports `--sign`, `--mark`, `--digest-check-count`,
`--no-banner-romtag` / `--no-rsa-appsplash`, and `--billstuff-romtag`.


### sign

Sign an existing image for retail playback.

```
3dt sign game.iso --output game-signed.iso
```

`--output` requires exactly one input. Useful flags include `--force` for
unusual source layouts, `--mark`, `--digest-check-count`,
`--no-banner-romtag` / `--no-rsa-appsplash`, and `--billstuff-romtag`.


### verify

Validate signed images and report status.

```
3dt verify --format=csv game.iso
```

Output formats are `human`, `csv`, and `json`; add `--no-digest-table` to skip
digest-table checks or `--quiet` to print only per-image verification status.


### sign-file

Sign, verify, or emit signatures for a file payload.
Use `--append` to append a new signature trailer, `--replace` to replace the
existing trailer, `--write` to update the input file, `--signature-output` to
write signature bytes separately, and `--key-name=app` or `--key-name=3do` to
choose the signing key.

```
3dt sign-file --verify boot_code
3dt sign-file --write --append --key-name app boot_code
```

### decrypt-file

Decrypt CD-DIPIR boot payloads. This is mainly used for the boot file
(`system/kernel/boot_code`, sometimes referred to as `boot_file`) when working
with CD-DIPIR-protected images. In practice that is the primary real-world use.
This maps to the Portfolio OS `src/dipir/cdipir.c` boot flow (`DecryptBlock()`).
Re-run `3dt encrypt-file` on the same file once edits are complete.

```
3dt decrypt-file boot_code
```

### encrypt-file

Encrypt CD-DIPIR boot payloads using the same boot-file obfuscation transform.
This is effectively the inverse operation of `decrypt-file` and is used when
you need to restore the encrypted form (typically for `system/kernel/boot_code`).
The inverse transform corresponds to Portfolio OS CD-DIPIR boot-file logic in
`src/dipir/cdipir.c`.

```
3dt encrypt-file boot_code
```


## Links

* https://3dodev.com
* https://github.com/trapexit/3dt
* https://github.com/trapexit/3it
* https://github.com/trapexit/modbin
* https://github.com/trapexit/3do-devkit
