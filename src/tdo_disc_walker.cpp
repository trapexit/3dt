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
#include "tdo_disc_walker.hpp"

#include <filesystem>

namespace fs = std::filesystem;
typedef TDO::DiscWalker::Callbacks Callbacks;


class Impl
{
public:
  Impl(std::istream &is_,
       Callbacks    &callbacks_)

    : _callbacks(callbacks_),
      _reader(is_)
  {

  }

public:
  Error
  walk(const uint32_t  block_,
       const uint32_t  block_size_,
       const fs::path &path_)
  {
    TDO::DirectoryHeader header;
    TDO::DirectoryRecord record;
    TDO::PosGuard pos_guard(_reader);

    _reader.disc_seek(block_ * block_size_);

    _reader.read(header);

    _reader.push_pos();
    _callbacks(path_,header,_reader);
    _reader.pop_pos();

    if(header.first_entry_offset == (uint32_t)-1)
      return {};

    _reader.disc_seek(header.disc_offset + header.first_entry_offset);

    while(_reader.good())
      {
        _reader.read(record);
        if(!_reader.good())
          break;

        _reader.push_pos();
        _callbacks(path_ / record.filename,record,_reader);
        _reader.pop_pos();

        if(record.is_directory())
          {
            walk(record.avatar_list[0],
                 record.block_size,
                 path_ / record.filename);
          }

        if(record.last_in_dir())
          break;

        if(record.last_in_block())
          {
            walk(block_ + 1,
                 block_size_,
                 path_);
            break;
          }
      }

    return {};
  }

  Error
  walk(const TDO::DiscLabel &label_)
  {
    fs::path path;

    return walk(label_.root_directory_avatar_list[0],
                label_.root_directory_block_size,
                path);
  }

  Error
  walk()
  {
    Error err;
    TDO::DiscLabel label;

    err = _reader.discover_image_format();
    if(err)
      return err;

    _reader.disc_seek(0);
    _reader.read(label);

    return walk(label);
  }

private:
  Callbacks       &_callbacks;
  TDO::DiscReader  _reader;
};

namespace TDO
{
  DiscWalker::DiscWalker(std::istream &is_,
                         Callbacks    &callbacks_)
    : _callbacks(callbacks_),
      _is(is_)
  {
  }

  DiscWalker::DiscWalker(TDO::DiscReader &reader_,
                         Callbacks       &callbacks_)
    : _callbacks(callbacks_),
      _is(reader_.istream())
  {
  }

  Error
  DiscWalker::walk()
  {
    Impl impl(_is,_callbacks);

    return impl.walk();
  }
}
