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

  static constexpr u32 BLOCK_SIZE = 2048;
  static constexpr u32 DIRECTORY_HEADER_SIZE = 20;
  static constexpr u32 DIRECTORY_RECORD_BASE_SIZE = 68;

  static
  u32
  record_size(const Entry &entry_)
  {
    return (DIRECTORY_RECORD_BASE_SIZE +
            (std::max<std::size_t>(1,entry_.avatar_list.size()) * sizeof(u32)));
  }

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
    std::array<char,4> buf;

    buf[0] = ((value_ >> 24) & 0xFF);
    buf[1] = ((value_ >> 16) & 0xFF);
    buf[2] = ((value_ >>  8) & 0xFF);
    buf[3] = ((value_ >>  0) & 0xFF);
    os_.write(&buf[0],buf.size());
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
    os_.seekp(static_cast<std::streamoff>(block_) * BLOCK_SIZE,std::ios::beg);
  }

  static
  void
  write_fixed_string(std::ostream      &os_,
                     const std::string &str_,
                     u32                size_)
  {
    std::vector<char> buf;

    buf.resize(size_,0);
    memcpy(buf.data(),str_.c_str(),std::min<std::size_t>(str_.size(),size_ - 1));
    os_.write(buf.data(),buf.size());
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
      throw std::runtime_error("too many root directory avatars");
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
    write_u32(os_,DIRECTORY_HEADER_SIZE);
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
    const std::vector<u32> avatars = entry_.avatar_list.empty() ? std::vector<u32>{entry_.start_block} : entry_.avatar_list;
    write_u32(os_,avatars.size() - 1);
    for(auto avatar : avatars)
      write_u32(os_,avatar);
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
    used          = DIRECTORY_HEADER_SIZE;
    for(const auto &child : dir_.children)
      {
        u32 size;

        size = record_size(*child);
        if((used + size) > BLOCK_SIZE)
          {
            if(current_block == block_index_)
              break;
            current_block++;
            used = DIRECTORY_HEADER_SIZE;
          }

        if(current_block == block_index_)
          entries.push_back(child.get());

        used += size;
      }

    first_free_byte_ = used;

    return entries;
  }

  static
  u32
  directory_used_block_count(const Entry &dir_)
  {
    u32 blocks;
    u32 used;

    if(dir_.children.empty())
      return 0;

    blocks = 1;
    used   = DIRECTORY_HEADER_SIZE;
    for(const auto &child : dir_.children)
      {
        u32 size;

        size = record_size(*child);
        if((used + size) > BLOCK_SIZE)
          {
            blocks++;
            used = DIRECTORY_HEADER_SIZE;
          }
        used += size;
      }

    return blocks;
  }

  static
  void
  write_directory(std::ostream &os_,
                  const Entry  &dir_)
  {
    if(!dir_.directory || (dir_.block_count == 0))
      return;

    const std::vector<u32> avatars = dir_.avatar_list.empty() ? std::vector<u32>{dir_.start_block} : dir_.avatar_list;
    const u32 used_block_count = directory_used_block_count(dir_);
    for(auto avatar : avatars)
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
              first_free_byte = DIRECTORY_HEADER_SIZE;
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
      }

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
      throw std::runtime_error("failed to open input file: " + entry_.src_path.string());

    const std::vector<u32> avatars = entry_.avatar_list.empty() ? std::vector<u32>{entry_.start_block} : entry_.avatar_list;
    for(auto avatar : avatars)
      {
        is.clear();
        is.seekg(0,std::ios::beg);
        seek_block(os_,avatar);
        util::copy_stream(is,os_,entry_.data_byte_count);
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

    os_.seekp((static_cast<std::streamoff>(total_blocks_) * BLOCK_SIZE) - 1,std::ios::beg);
    os_.put(0);
    if(!os_)
      throw std::runtime_error("failed to resize output image");
  }

  static
  void
  validate_manifest(const TDO::DiscManifest &manifest_)
  {
    if(manifest_.output.empty())
      throw std::runtime_error("manifest has no output path");
    if(!manifest_.root.directory)
      throw std::runtime_error("manifest root is not a directory");
    if(manifest_.root.block_count == 0)
      throw std::runtime_error("manifest root directory is empty");
    if(manifest_.total_blocks == 0)
      throw std::runtime_error("manifest has no allocated blocks");
  }
}

Error
TDO::pack_disc_image(const TDO::DiscManifest &manifest_)
{
  try
    {
      TDO::DiscLabel label;
      std::ofstream os;

      validate_manifest(manifest_);
      label = make_disc_label(manifest_);

      os.open(manifest_.output,std::ios::binary|std::ios::trunc);
      if(!os)
        return "failed to open output image";

      resize_output(os,manifest_.total_blocks);
      write_disc_label(os,label);
      write_directory(os,manifest_.root);
      write_file_data(os,manifest_.root);

      os.close();

      return {};
    }
  catch(const std::exception &e)
    {
      return e.what();
    }
}
