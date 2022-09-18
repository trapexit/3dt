/*
  ISC License

  Copyright (c) 2021, Antonio SJ Musumeci <trapexit@spawn.link>

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

#include "error.hpp"
#include "tdo_romtag.hpp"
#include "tdo_fs_walker.hpp"

#include <cstring>
#include <filesystem>
#include <vector>


namespace fs = std::filesystem;
typedef TDO::FSWalker::Callbacks Callbacks;


static
void
update_record(const TDO::ROMTagVec &tags_,
              TDO::DirectoryRecord &dr_)
{
  for(const auto &tag : tags_)
    {
      for(uint32_t i = 0; i <= dr_.last_avatar_index; i++)
        {
          if(tag.offset+1 == dr_.avatar_list[i])
            {
              dr_.byte_count = tag.size;
              return;
            }
        }
    }
}

class Impl
{
public:
  Impl(std::istream &is_,
       Callbacks    &callbacks_)

    : _callbacks(callbacks_),
      _stream(is_)
  {

  }

public:
  Error
  walk_v1_dir_block(const TDO::DiscLabel &label_,
                    const TDO::ROMTagVec &romtags_,
                    const std::int64_t    dir_hdr_pos_,
                    const fs::path       &path_)
  {
    std::int64_t pos;
    TDO::DirectoryHeader dh;

    pos = dir_hdr_pos_;
    _stream.data_byte_seek(pos);
    _stream.read(dh);

    _callbacks(path_,dh,_stream);

    pos += dh.first_entry_offset;
    _stream.data_byte_seek(pos);

    while(true)
      {
        uint32_t dr_pos;
        TDO::DirectoryRecord dr;

        dr_pos = _stream.file_tell();
        _stream.read(dr);
        update_record(romtags_,dr);

        {
          TDO::PosGuard guard(_stream);
          _callbacks(path_ / dr.filename,dr,dr_pos,_stream);
        }

        if(dr.is_directory())
          walk_v1_dir(label_,romtags_,dr,path_ / dr.filename);

        if(dr.last_in_dir())
          break;
        if(dr.last_in_block())
          break;
        if(_stream.data_byte_tell() >= (dir_hdr_pos_ + dh.first_free_byte))
          break;
      }

    return {};
  }

  Error
  walk_v1_dir(const TDO::DiscLabel &label_,
              const TDO::ROMTagVec &romtags_,
              const std::uint32_t   dir_block_,
              const std::uint32_t   dir_block_size_,
              const std::uint32_t   dir_block_count_,
              const fs::path       &path_)
  {
    std::int64_t pos;
    TDO::PosGuard pos_guard(_stream);

    pos = (dir_block_ * label_.volume_block_size);
    for(std::uint32_t block = 0; block < dir_block_count_; block++)
      {
        walk_v1_dir_block(label_,romtags_,pos,path_);
        pos += dir_block_size_;
      }

    return {};
  }

  Error
  walk_v1_dir(const TDO::DiscLabel       &label_,
              const TDO::ROMTagVec       &romtags_,
              const TDO::DirectoryRecord &parent_,
              const fs::path             &path_)
  {
    return walk_v1_dir(label_,
                       romtags_,
                       parent_.avatar_list[0],
                       parent_.block_size,
                       parent_.block_count,
                       path_);
  }

  Error
  walk_v1_root_dir(const TDO::DiscLabel &label_,
                   const TDO::ROMTagVec &romtags_,
                   const fs::path       &path_)
  {
    return walk_v1_dir(label_,
                       romtags_,
                       label_.root_directory_avatar_list[0],
                       label_.root_directory_block_size,
                       label_.root_directory_block_count,
                       path_);
  }

  Error
  walk_v2(const TDO::DiscLabel &label_,
          const fs::path       &path_)
  {
    std::uint32_t pos;

    pos = label_.root_directory_avatar_list[0] * label_.root_directory_block_size;
    while(true)
      {
        TDO::DirectoryRecord dr;
        TDO::LinkedMemFileEntry lmfe;
        _stream.data_byte_seek(pos);
        _stream.read(lmfe);

        if(lmfe.fingerprint == FINGERPRINT_FILEBLOCK)
          {
            dr.flags = 0;
            dr.unique_identifier = lmfe.unique_identifier;
            dr.type = lmfe.type;
            dr.block_size = label_.volume_block_size;
            dr.byte_count = lmfe.byte_count;
            dr.block_count = lmfe.byte_count;
            dr.burst = 0;
            dr.gap = 0;
            memcpy(dr.filename,lmfe.filename,sizeof(dr.filename));
            dr.last_avatar_index = 0;
            dr.avatar_list.push_back(pos + lmfe.header_block_count);

            TDO::PosGuard guard(_stream);
            _callbacks(path_ / dr.filename,dr,pos,_stream);
          }

        if(lmfe.flink_offset < pos)
          break;
        pos = lmfe.flink_offset;
      }

    return {};
  }

  Error
  walk()
  {
    Error err;
    fs::path path;
    TDO::DiscLabel dl;
    TDO::ROMTagVec romtags;

    err = _stream.setup();
    if(err)
      return err;

    _stream.read(dl);
    romtags = _stream.romtags();

    _callbacks.begin();
    switch(dl.volume_structure_version)
      {
      case VOLUME_STRUCTURE_OPERA_READONLY:
        err = walk_v1_root_dir(dl,romtags,path);
        break;
      case VOLUME_STRUCTURE_LINKED_MEM:
        err = walk_v2(dl,path);
        break;
      default:
        break;
      }
    _callbacks.end();

    return err;
  }

private:
  Callbacks      &_callbacks;
  TDO::DevStream  _stream;
};

namespace TDO
{
  FSWalker::FSWalker(std::istream &is_,
                     Callbacks    &callbacks_)
    : _callbacks(callbacks_),
      _is(is_)
  {
  }

  FSWalker::FSWalker(DevStream &stream_,
                     Callbacks &callbacks_)
    : _callbacks(callbacks_),
      _is(stream_.istream())
  {
  }

  Error
  FSWalker::walk()
  {
    Impl impl(_is,_callbacks);

    return impl.walk();
  }
}
