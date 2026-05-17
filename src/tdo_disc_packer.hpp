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
#include "tdo_disc_format.hpp"
#include "tdo_disc_label.hpp"
#include "types_ints.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace TDO
{
  enum class DiscManifestEntryKind
  {
    Normal,
    DiscLabel,
    ROMTags,
    Signatures,
  };

  struct DiscManifestEntry
  {
    typedef std::unique_ptr<DiscManifestEntry> Ptr;

    std::filesystem::path              src_path;
    std::string                        name;
    DiscManifestEntryKind              kind;
    bool                               directory;
    u32                                unique_identifier;
    u32                                type;
    u32                                flags;
    u32                                block_size;
    u32                                byte_count;
    u32                                data_byte_count;
    u32                                block_count;
    u32                                burst;
    u32                                gap;
    u32                                start_block;
    u32                                record_file_offset;
    u32                                record_size;
    std::vector<u32>                   avatar_list;
    std::vector<DiscManifestEntry::Ptr> children;
  };

  struct DiscManifest
  {
    std::filesystem::path output;
    TDO::DiscLabel        disc_label;
    u32                   total_blocks;
    bool                  replay_layout;
    DiscManifestEntry     root;
  };

  void pack_disc_image(const DiscManifest &manifest);

  constexpr u32 DIRECTORY_HEADER_SIZE = 20;
  constexpr u32 DIRECTORY_RECORD_BASE_SIZE = 68;

  static inline
  u32
  record_size(const DiscManifestEntry &entry_)
  {
    return (DIRECTORY_RECORD_BASE_SIZE +
            (std::max<std::size_t>(1,entry_.avatar_list.size()) * sizeof(u32)));
  }

  static inline
  u32
  directory_block_count(const DiscManifestEntry &dir_)
  {
    u32 blocks;
    u32 used;

    if(dir_.children.empty())
      return 0;

    blocks = 1;
    used   = DIRECTORY_HEADER_SIZE;
    for(const auto &child : dir_.children)
      {
        u32 size = record_size(*child);
        if((used + size) > BLOCK_SIZE)
          {
            blocks++;
            used = DIRECTORY_HEADER_SIZE;
          }
        used += size;
      }

    return blocks;
  }
}
