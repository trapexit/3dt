/*
  ISC License

  Copyright (c) 2025, Antonio SJ Musumeci <trapexit@spawn.link>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#pragma once

#include "error.hpp"
#include "fmt.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#define DR_FLAG_IS_DIRECTORY      0x00000001
#define DR_FLAG_IS_READONLY       0x00000002
#define DR_FLAG_IS_FOR_FILESYSTEM 0x00000004
#define DR_FLAG_LAST_IN_BLOCK     0x40000000
#define DR_FLAG_LAST_IN_DIR       0x80000000
#define DR_FLAG_LAST_IN_MASK      (DR_FLAG_LAST_IN_BLOCK|DR_FLAG_LAST_IN_DIR)

#define DR_TYPE_DIRECTORY         0x2a646972
#define DR_TYPE_LABEL             0x2a6c626c
#define DR_TYPE_CATAPULT          0x2a7a6170


namespace TDO
{
  typedef std::vector<uint32_t> U32Vec;

  struct DirectoryRecord
  {
  public:
    uint32_t flags;
    uint32_t unique_identifier;
    uint32_t type;
    uint32_t block_size;
    uint32_t byte_count;
    uint32_t block_count;
    uint32_t burst;
    uint32_t gap;
    char     filename[32];
    uint32_t last_avatar_index;
    U32Vec   avatar_list;

  public:
    bool is_directory() const { return !!(flags & DR_FLAG_IS_DIRECTORY); }
    bool is_readonly() const { return !!(flags & DR_FLAG_IS_READONLY); }
    bool is_for_fs() const { return !!(flags & DR_FLAG_IS_FOR_FILESYSTEM); }

  public:
    bool last_in_block() const { return !!(flags & DR_FLAG_LAST_IN_BLOCK); }
    bool last_in_dir() const { return !!(flags & DR_FLAG_LAST_IN_DIR); }

  public:
    // OperaFS targets a 32-bit platform; both the avatar block index
    // and the byte offset must fit in u32. Compute the multiply in
    // u64 so we can detect a malformed image whose record drives the
    // product past UINT32_MAX, then narrow. Throwing here surfaces
    // the bad record to the calling listing/unpack path's per-image
    // try/catch rather than silently displaying a wrapped offset.
    uint32_t disc_avatar_offset(const uint32_t i) const
    {
      const uint64_t offset =
        static_cast<uint64_t>(avatar_list[i]) * block_size;
      if(offset > std::numeric_limits<uint32_t>::max())
        throw Error("avatar disc offset out of 32-bit range: " +
                    std::to_string(offset));
      return static_cast<uint32_t>(offset);
    }
    uint32_t disc_avatar_offset() const { return disc_avatar_offset(0); }

  public:
    std::string
    type_str() const
    {
      std::string str;
      const char *type_buf = (const char*)&type;

      str.push_back(std::isprint(type_buf[3]) ? type_buf[3] : ' ');
      str.push_back(std::isprint(type_buf[2]) ? type_buf[2] : ' ');
      str.push_back(std::isprint(type_buf[1]) ? type_buf[1] : ' ');
      str.push_back(std::isprint(type_buf[0]) ? type_buf[0] : ' ');

      return str;
    }

    std::string
    type_hex() const
    {
      return fmt::format("{:08x}",type);
    }
  };
}
