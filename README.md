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
Usage: ./build/3dt [OPTIONS] SUBCOMMAND

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
```


### list

A `ls` or `dir` style listing of the disc's contents.

```
3dt list ./PO\'ed.iso | head
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
```

You can also filter based on a path prefix.

```
3dt list ./PO\'ed.iso System/Kernel
drf        2048 0x2b52669f 0x2a646972 (*dir) System/Kernel
-r-        8192 0x125f3f52 0x20202020 (    ) System/Kernel/boot_code
-r-        2908 0x2000c05f 0x20202020 (    ) System/Kernel/misc_code
-r-      115520 0x2cf3d1e8 0x20202020 (    ) System/Kernel/os_code
```


### info

Prints out the disc's "disc label", file count, and total data byte size.

```
3dt info ./PO\'ed.iso
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

The 3dt internal database includes a number of fields and metrics found on a 3DO image. The "unique" identifiers are not necessarily unique and as such the file count and total data size were used to help in uniquly identifying images. If you come across an unknown image (which isn't just random homebrew) please file a [ticket](https://github.com/trapexit/3dt/issues) with the details.

```
3dt identify ./PO\'ed.iso
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

This will copy the files from the disc image to your local storage device. It will always create a directory based on the file name with ".unpacked" appended. Use the `-o,--output` option to change the directory it unpacks to.

```
3dt unpack ./PO\'ed.iso
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
3dt rename ./POed.iso
./POed.iso: renamed to ./PO'ed (USA, Europe).iso
```


### to-iso

This, like many other tools, will take a raw CDROM image such as a .bin file from a .bin/.cue set and create a .iso file.

```
3dt to-iso ./PO\'ed\ \(USA\,\ Europe\).bin
./PO'ed (USA, Europe).iso: sector 205249 of 205249 written

3dt identify PO\'ed\ \(USA\,\ Europe\).iso
PO'ed (USA, Europe).iso:
 - volume_unique_identifier: 0x198EEB79
 - volume_block_count: 204800
 - root_unique_identifier: 0x1F2377CF
 - file_count: 398
 - total_data_size: 80414372
 - matches:
   - PO'ed (USA, Europe)
```


## Links

* https://3dodev.com
* https://github.com/trapexit/3dt
* https://github.com/trapexit/3it
* https://github.com/trapexit/modbin
* https://github.com/trapexit/3do-devkit
