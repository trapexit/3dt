# 3dt: 3DO Disc Tool

## Features

* list OperaFS content
* list lowlevel disc/OperaFS details
* list ROM tags
* identify discs and ROMs
* rename image based on identification
* unpack disc and ROM OperaFS contents
* convert raw disc images to .iso
* supports reading CD-ROM Mode 1 images and 2048 byte/sector ISOs


## Usage

### --help

```
3dt: 3DO Disc Tool
Usage: 3dt [OPTIONS] SUBCOMMAND

Options:
  -h,--help                   Print this help message and exit
  --help-all

Subcommands:
  version                     print 3dt version
  list                        list disc content
  info                        prints lowlevel info on disc
  identify                    attempt to identify disc image
  unpack                      unpack disc image
  rename                      rename disc image as identified
  to-iso                      convert a .bin or similar disc image to .iso
  romtags                     print out image romtags
```

Use `--help` on individual subcommands to get specific subcmd options.


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

```
$ 3dt rename ./POed.iso
./POed.iso: renamed to ./PO'ed (USA, Europe).iso
```


### to-iso

This, like many other tools, will take a raw CDROM image such as a .bin file from a .bin/.cue set and create a .iso file.

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

```
$ 3dt romtags Wolfenstein\ 3D\ \(USA\).iso
File,SubSysType,Type,Version,Revision,Flags,TypeSpecific,Offset,Size
Wolfenstein 3D (USA).iso,0x0f,NEWKNEWNEWGNUBOOT,2,5,0,0,1,6448
Wolfenstein 3D (USA).iso,0x0f,OS,24,225,0,0,5,115520
Wolfenstein 3D (USA).iso,0x0f,BILLSTUFF,0,0,0,0,2893249791,0
Wolfenstein 3D (USA).iso,0x0f,BLOCKS_ALWAYS,0,0,0,0,42713,47
Wolfenstein 3D (USA).iso,0x0f,MISCCODE,0,0,0,0,69,2908
Wolfenstein 3D (USA).iso,0x0f,SIGNATURE_BLOCK,0,0,0,15,42766,57344
```


## Links

* https://3dodev.com
* https://github.com/trapexit/3dt
* https://github.com/trapexit/3it
* https://github.com/trapexit/modbin
* https://github.com/trapexit/3do-devkit
