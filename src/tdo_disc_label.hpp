#pragma once

#include <array>
#include <cstdint>

/*
  Define the position of the primary label on each Opera disc, the
  block offset between avatars, and the index of the last avatar
  (i.e. the avatar count minus one).  The latter figure *must* match
  the ROOT_HIGHEST_AVATAR figure from "filesystem.h", as the same
  File structure is use to read the label at boot time, and to provide
  access to the root directory.
*/

#define DISC_BLOCK_SIZE           2048
#define DISC_LABEL_OFFSET         225
#define DISC_LABEL_AVATAR_DELTA   32786
#define DISC_LABEL_HIGHEST_AVATAR 7
#define DISC_TOTAL_BLOCKS         330000

#define ROOT_HIGHEST_AVATAR     7
#define FILESYSTEM_MAX_NAME_LEN 32

#define VOLUME_STRUCTURE_OPERA_READONLY 1
#define VOLUME_STRUCTURE_LINKED_MEM     2

#define VOLUME_SYNC_BYTE     0x5A
#define VOLUME_SYNC_BYTE_LEN 5
#define VOLUME_COM_LEN       32
#define VOLUME_ID_LEN        32

/*
  This disc won't necessarily cause a reboot when inserted.  This flag is
  advisory ONLY. Only by checking with cdromdipir can you be really sure.
  Place in dl_VolumeFlags.  Note: the first volume gets this flag also.
*/
#define	VOLUME_FLAGS_DATADISC 0x01

namespace TDO
{
  typedef std::array<char,VOLUME_SYNC_BYTE_LEN>      VSBArray;
  typedef std::array<char,VOLUME_COM_LEN>            VCIArray;
  typedef std::array<uint32_t,ROOT_HIGHEST_AVATAR+1> RDAArray;

  struct DiscLabel
  {
  public:
    uint32_t file_offset;
    uint32_t data_offset;

  public:
    char     record_type;       /* Should equal 0x01 */
    VSBArray volume_sync_bytes; /* Synchronization bytes: 0x5A5A5A5A5A */
    char     volume_structure_version; /* Should equal 0x01 */
    char     volume_flags;      /* Should equal 0x00 */
    VCIArray volume_commentary; /* volume/disc description */
    VCIArray volume_identifier; /* volume/disc name */
    uint32_t volume_unique_identifier; /* Roll a billion-sided die */
    uint32_t volume_block_size; /* Usually contains 2048 */
    uint32_t volume_block_count; /* # of blocks on disc */
    uint32_t root_unique_identifier; /* Roll a billion-sided die */
    uint32_t root_directory_block_count; /* # of blocks in root */
    uint32_t root_directory_block_size; /* usually same as vol blk size */
    uint32_t root_directory_last_avatar_index; /* should contain 7 */
    RDAArray root_directory_avatar_list;
  };
}
