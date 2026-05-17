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

#include "tdo_disc_packer.hpp"

#include "copy_stream.hpp"
#include "tdo_directory_record.hpp"
#include "tdo_disc_format.hpp"
#include "tdo_disc_label.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace
{
  using Entry = TDO::DiscManifestEntry;
  using EntryKind = TDO::DiscManifestEntryKind;

  static
  void
  write_u8(std::ostream &os_,
           u8            value_)
  {
    os_.put(static_cast<char>(value_));
  }

  static
  void
  write_u32(std::ostream &os_,
            u32           value_)
  {
    u32 be = __builtin_bswap32(value_);
    os_.write(reinterpret_cast<const char*>(&be), sizeof(be));
  }

  static
  void
  write_s32(std::ostream &os_,
            s32           value_)
  {
    write_u32(os_,static_cast<u32>(value_));
  }

  static
  void
  write_bytes(std::ostream &os_,
              const char   *data_,
              u64           size_)
  {
    os_.write(data_,size_);
  }

  static
  void
  seek_block(std::ostream &os_,
             u32           block_)
  {
    os_.seekp(static_cast<std::streamoff>(block_) * TDO::BLOCK_SIZE,std::ios::beg);
  }

  static
  void
  write_fixed_string(std::ostream      &os_,
                     const std::string &str_,
                     u32                size_)
  {
    const u32 len = std::min<u32>(str_.size(), size_ - 1);
    os_.write(str_.c_str(), len);
    const u32 padding = size_ - len;
    if(padding > 0)
      {
        std::vector<char> zeros(padding, '\0');
        os_.write(zeros.data(), padding);
      }
  }

  static
  TDO::DiscLabel
  make_disc_label(const TDO::DiscManifest &manifest_)
  {
    TDO::DiscLabel label;

    label = manifest_.disc_label;

    label.volume_block_count = manifest_.total_blocks;
    label.root_directory_block_count = manifest_.root.block_count;
    label.root_directory_block_size = manifest_.root.block_size;
    label.root_directory_last_avatar_index = manifest_.root.avatar_list.empty() ? 0 : manifest_.root.avatar_list.size() - 1;
    label.root_directory_avatar_list.fill(0);
    if(manifest_.root.avatar_list.size() > label.root_directory_avatar_list.size())
      throw Error("too many root directory avatars");
    for(std::size_t i = 0; i < manifest_.root.avatar_list.size(); i++)
      label.root_directory_avatar_list[i] = manifest_.root.avatar_list[i];

    return label;
  }

  static
  void
  write_disc_label(std::ostream         &os_,
                   const TDO::DiscLabel &label_)
  {
    seek_block(os_,0);
    write_u8(os_,label_.record_type);
    write_bytes(os_,&label_.volume_sync_bytes[0],label_.volume_sync_bytes.size());
    write_u8(os_,label_.volume_structure_version);
    write_u8(os_,label_.volume_flags);
    write_bytes(os_,&label_.volume_commentary[0],label_.volume_commentary.size());
    write_bytes(os_,&label_.volume_identifier[0],label_.volume_identifier.size());
    write_u32(os_,label_.volume_unique_identifier);
    write_u32(os_,label_.volume_block_size);
    write_u32(os_,label_.volume_block_count);
    write_u32(os_,label_.root_unique_identifier);
    write_u32(os_,label_.root_directory_block_count);
    write_u32(os_,label_.root_directory_block_size);
    write_u32(os_,label_.root_directory_last_avatar_index);
    for(auto block : label_.root_directory_avatar_list)
      write_u32(os_,block);
  }

  static
  void
  write_directory_header(std::ostream &os_,
                         s32           next_block_,
                         s32           prev_block_,
                         u32           first_free_byte_)
  {
    write_s32(os_,next_block_);
    write_s32(os_,prev_block_);
    write_u32(os_,0);
    write_u32(os_,first_free_byte_);
    write_u32(os_,TDO::DIRECTORY_HEADER_SIZE);
  }

  static
  void
  write_directory_record(std::ostream &os_,
                         const Entry  &entry_,
                         u32           extra_flags_)
  {
    write_u32(os_,entry_.flags | extra_flags_);
    write_u32(os_,entry_.unique_identifier);
    write_u32(os_,entry_.type);
    write_u32(os_,entry_.block_size);
    write_u32(os_,entry_.byte_count);
    write_u32(os_,entry_.block_count);
    write_u32(os_,entry_.burst);
    write_u32(os_,entry_.gap);
    write_fixed_string(os_,entry_.name,FILESYSTEM_MAX_NAME_LEN);
    if(entry_.avatar_list.empty())
      {
        write_u32(os_,0);
        write_u32(os_,entry_.start_block);
      }
    else
      {
        write_u32(os_,entry_.avatar_list.size() - 1);
        for(auto avatar : entry_.avatar_list)
          write_u32(os_,avatar);
      }
  }

  static
  std::vector<const Entry*>
  directory_block_entries(const Entry &dir_,
                          u32         block_index_,
                          u32        &first_free_byte_)
  {
    std::vector<const Entry*> entries;
    u32 current_block;
    u32 used;

    current_block = 0;
    used          = TDO::DIRECTORY_HEADER_SIZE;
    for(const auto &child : dir_.children)
      {
        u32 size;

        size = TDO::record_size(*child);
        if((used + size) > TDO::BLOCK_SIZE)
          {
            if(current_block == block_index_)
              break;
            current_block++;
            used = TDO::DIRECTORY_HEADER_SIZE;
          }

        if(current_block == block_index_)
          entries.push_back(child.get());

        used += size;
      }

    first_free_byte_ = used;

    return entries;
  }

  static
  void
  write_directory(std::ostream &os_,
                  const Entry  &dir_)
  {
    if(!dir_.directory || (dir_.block_count == 0))
      return;

    const u32 used_block_count = TDO::directory_block_count(dir_);
    auto write_avatar = [&](u32 avatar)
    {
      for(u32 i = 0; i < dir_.block_count; i++)
        {
          s32 next_block;
          s32 prev_block;
          u32 first_free_byte;
          std::vector<const Entry*> entries;

          if(i < used_block_count)
            entries = directory_block_entries(dir_,i,first_free_byte);
          else
            first_free_byte = TDO::DIRECTORY_HEADER_SIZE;
          next_block = ((i + 1) < dir_.block_count ? i + 1 : -1);
          prev_block = (i > 0 ? i - 1 : -1);

          seek_block(os_,avatar + i);
          write_directory_header(os_,next_block,prev_block,first_free_byte);
          for(std::size_t j = 0; j < entries.size(); j++)
            {
              u32 flags;

              flags = 0;
              if((j + 1) == entries.size())
                {
                  flags = DR_FLAG_LAST_IN_BLOCK;
                  if((i + 1) == used_block_count)
                    flags |= DR_FLAG_LAST_IN_DIR;
                }
              write_directory_record(os_,*entries[j],flags);
            }
        }
    };
    if(dir_.avatar_list.empty())
      write_avatar(dir_.start_block);
    else
      for(auto avatar : dir_.avatar_list)
        write_avatar(avatar);

    for(const auto &child : dir_.children)
      write_directory(os_,*child);
  }

  static
  void
  copy_file_data(std::ostream &os_,
                 const Entry  &entry_)
  {
    std::ifstream is;

    if(entry_.kind != EntryKind::Normal)
      return;
    if(entry_.directory || (entry_.block_count == 0))
      return;

    is.open(entry_.src_path,std::ios::binary);
    if(!is)
      throw Error("failed to open input file: " + entry_.src_path.string());

    if(entry_.avatar_list.empty())
      {
        is.clear();
        is.seekg(0,std::ios::beg);
        seek_block(os_,entry_.start_block);
        util::copy_stream(is,os_,entry_.data_byte_count);
      }
    else
      {
        for(auto avatar : entry_.avatar_list)
          {
            is.clear();
            is.seekg(0,std::ios::beg);
            seek_block(os_,avatar);
            util::copy_stream(is,os_,entry_.data_byte_count);
          }
      }
  }

  static
  void
  write_file_data(std::ostream &os_,
                  const Entry  &entry_)
  {
    if(!entry_.directory)
      copy_file_data(os_,entry_);

    for(const auto &child : entry_.children)
      write_file_data(os_,*child);
  }

  static
  void
  resize_output(std::ostream &os_,
                u32           total_blocks_)
  {
    if(total_blocks_ == 0)
      return;

    os_.seekp((static_cast<std::streamoff>(total_blocks_) * TDO::BLOCK_SIZE) - 1,std::ios::beg);
    os_.put(0);
    if(!os_)
      throw Error("failed to resize output image");
  }

  static
  void
  validate_manifest(const TDO::DiscManifest &manifest_)
  {
    if(manifest_.output.empty())
      throw Error("manifest has no output path");
    if(!manifest_.root.directory)
      throw Error("manifest root is not a directory");
    if(manifest_.root.block_count == 0)
      throw Error("manifest root directory is empty");
    if(manifest_.total_blocks == 0)
      throw Error("manifest has no allocated blocks");
  }
}

void
TDO::pack_disc_image(const TDO::DiscManifest &manifest_)
{
  TDO::DiscLabel label;
  std::ofstream os;

  validate_manifest(manifest_);
  label = make_disc_label(manifest_);

  os.open(manifest_.output,std::ios::binary|std::ios::trunc);
  if(!os)
    throw Error("failed to open output image");

  resize_output(os,manifest_.total_blocks);
  write_disc_label(os,label);
  write_directory(os,manifest_.root);
  write_file_data(os,manifest_.root);

  os.close();
}
