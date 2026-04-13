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
#include <string>
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

static
Error
decode_v1_filename(const TDO::DirectoryRecord &dr_,
                   std::string                &filename_)
{
  static constexpr std::size_t filename_size = 32;

  const void *terminator;

  terminator = memchr(dr_.filename,'\0',filename_size);
  if(terminator != nullptr)
    {
      const std::size_t length = static_cast<const char*>(terminator) - dr_.filename;

      if(length == 0)
        return "invalid empty directory record filename";

      filename_.assign(dr_.filename,length);
    }
  else
    {
      filename_.assign(dr_.filename,filename_size);
    }

  if(filename_.empty())
    return "invalid empty directory record filename";
  if(filename_ == "." || filename_ == "..")
    return "invalid directory record filename";
  if(filename_.find_first_of("/\\") != std::string::npos)
    return "invalid directory record filename";

  return Error();
}

static
Error
validate_v1_dir_block_bounds(const std::int64_t block_start_,
                             const std::uint32_t block_size_,
                             const std::int64_t pos_,
                             const char         *what_)
{
  if(block_size_ == 0)
    return {"invalid OperaFS directory block size"};
  if(pos_ < block_start_)
    return {std::string("invalid OperaFS ") + what_ + ": before directory block"};

  const std::int64_t block_end = block_start_ + static_cast<std::int64_t>(block_size_);

  if(pos_ > block_end)
    return {std::string("invalid OperaFS ") + what_ + ": beyond directory block"};

  return Error();
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
                      const std::int64_t    active_dir_byte_pos_,
                      const std::int64_t    active_dir_end_,
                      const std::int64_t    dh_data_byte_pos_,
                      const std::uint32_t   dir_block_size_,
                      const fs::path       &path_)
  {
    TDO::DirectoryHeader dh;
    std::int64_t data_byte_pos;
    Error err;

    if(dir_block_size_ < sizeof(TDO::DirectoryHeader))
      return {"invalid OperaFS directory block: smaller than directory header"};

    data_byte_pos = dh_data_byte_pos_;
    _stream.data_byte_seek(data_byte_pos);
    _stream.read(dh);

    _callbacks(path_,dh,_stream);

    data_byte_pos += dh.first_entry_offset;
    err = validate_v1_dir_block_bounds(dh_data_byte_pos_,dir_block_size_,data_byte_pos,"directory entry offset");
    if(err)
      return err;

    const std::int64_t first_free_byte_pos =
      dh_data_byte_pos_ + static_cast<std::int64_t>(dh.first_free_byte);

    err = validate_v1_dir_block_bounds(dh_data_byte_pos_,dir_block_size_,first_free_byte_pos,"directory free offset");
    if(err)
      return err;
    if(data_byte_pos == first_free_byte_pos)
      return Error();

    _stream.data_byte_seek(data_byte_pos);

    while(true)
      {
        const std::int64_t dr_pos = _stream.data_byte_tell();
        const std::uint32_t dr_file_pos = _stream.file_tell();
        TDO::DirectoryRecord dr;
        std::string decoded_filename;
        std::int64_t next_pos;

        if(dr_pos < data_byte_pos)
          return {"invalid OperaFS directory read position: reversed record offset"};
        if(dr_pos >= first_free_byte_pos)
          break;
        err = validate_v1_dir_block_bounds(dh_data_byte_pos_,dir_block_size_,dr_pos,"directory record offset");
        if(err)
          return err;

        _stream.read(dr);

        next_pos = _stream.data_byte_tell();
        if(next_pos <= dr_pos)
          return {"invalid OperaFS directory read position: non-advancing record read"};
        if(next_pos > first_free_byte_pos)
          return {"invalid OperaFS directory record: extends beyond directory data"};
        err = validate_v1_dir_block_bounds(dh_data_byte_pos_,dir_block_size_,next_pos,"directory record end");
        if(err)
          return err;

        update_record(romtags_,dr);
        err = decode_v1_filename(dr,decoded_filename);
        if(err)
          return err;

        {
          TDO::PosGuard guard(_stream);
          _callbacks(path_ / decoded_filename,dr,dr_file_pos,_stream);
        }

        if(dr.is_directory())
          {
            const std::int64_t child_dir_byte_pos =
              static_cast<std::int64_t>(dr.avatar_list[0]) * label_.volume_block_size;

            if((child_dir_byte_pos >= active_dir_byte_pos_) && (child_dir_byte_pos < active_dir_end_))
              return {"invalid OperaFS directory recursion target: non-advancing child directory block"};

            err = walk_v1_dir(label_,romtags_,dr,path_ / decoded_filename);
            if(err)
              return err;
          }

        if(dr.last_in_dir())
          break;
        if(dr.last_in_block())
          break;
        if(_stream.data_byte_tell() >= first_free_byte_pos)
          break;

        data_byte_pos = next_pos;
      }

    return Error();
  }

  Error
  walk_v1_dir(const TDO::DiscLabel &label_,
              const TDO::ROMTagVec &romtags_,
              const std::uint32_t   dir_block_,
              const std::uint32_t   dir_block_size_,
              const std::uint32_t   dir_block_count_,
              const fs::path       &path_)
  {
    std::int64_t  dh_data_byte_pos;
    const std::int64_t active_dir_byte_pos = (dir_block_ * label_.volume_block_size);
    const std::int64_t active_dir_end =
      active_dir_byte_pos + (static_cast<std::int64_t>(dir_block_size_) * dir_block_count_);
    TDO::PosGuard pos_guard(_stream);

    if((dir_block_count_ != 0) && (dir_block_size_ == 0))
      return {"invalid OperaFS directory metadata: zero directory block size"};

    dh_data_byte_pos = active_dir_byte_pos;
    for(std::uint32_t block = 0; block < dir_block_count_; block++)
      {
        Error err;

        err = walk_v1_dir_block(label_,
                                romtags_,
                                active_dir_byte_pos,
                                active_dir_end,
                                dh_data_byte_pos,
                                dir_block_size_,
                                path_);
        if(err)
          return err;
        dh_data_byte_pos += dir_block_size_;
      }

    return Error();
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
    Error err;

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

    return Error();
  }

  Error
  walk()
  {
    Error err;
    fs::path path;
    TDO::DiscLabel dl;
    TDO::ROMTagVec romtags;

    _stream.setup();

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
    try
      {
        Impl impl(_is,_callbacks);

        return impl.walk();
      }
    catch(const Error &err)
      {
        return err;
      }
  }
}
