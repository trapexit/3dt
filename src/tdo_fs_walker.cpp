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
#include "tdo_fs_walker.hpp"

#include <filesystem>

namespace fs = std::filesystem;
typedef TDO::FSWalker::Callbacks Callbacks;


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
  walk_dir_block(const TDO::DiscLabel &label_,
                 const fs::path       &path_)
  {
    while(true)
      {
        TDO::DirectoryRecord dr;

        _stream.read(dr);

        {
          TDO::PosGuard guard(_stream);
          _callbacks(path_ / dr.filename,dr,_stream);
        }

        if(dr.is_directory())
          walk_dir(label_,dr,path_ / dr.filename);

        if(dr.last_in_dir())
          break;
        if(dr.last_in_block())
          break;
      }

    return {};
  }

  Error
  walk_dir(const TDO::DiscLabel &label_,
           const std::uint32_t   dir_block_,
           const std::uint32_t   dir_block_size_,
           const std::uint32_t   dir_block_count_,
           const fs::path       &path_)
  {
    TDO::PosGuard pos_guard(_stream);
    std::int64_t pos;
    TDO::DirectoryHeader dh;

    pos = (dir_block_ * label_.volume_block_size);

    _stream.data_byte_seek(pos);
    _stream.read(dh);

    _callbacks(path_,dh,_stream);

    pos += dh.first_entry_offset;
    for(std::uint32_t block = 0; block < dir_block_count_; block++)
      {
        _stream.data_byte_seek(pos);
        walk_dir_block(label_,path_);
        pos += dir_block_size_;
      }

    return {};
  }

  Error
  walk_dir(const TDO::DiscLabel       &label_,
           const TDO::DirectoryRecord &parent_,
           const fs::path             &path_)
  {
    return walk_dir(label_,
                    parent_.avatar_list[0],
                    parent_.block_size,
                    parent_.block_count,
                    path_);
  }

  Error
  walk_root_dir(const TDO::DiscLabel &label_,
                const fs::path       &path_)
  {
    return walk_dir(label_,
                    label_.root_directory_avatar_list[0],
                    label_.root_directory_block_size,
                    label_.root_directory_block_count,
                    path_);
  }

  Error
  walk()
  {
    Error err;
    fs::path path;
    TDO::DiscLabel label;

    err = _stream.setup();
    if(err)
      return err;

    _stream.read(label);

    return walk_root_dir(label,path);
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
