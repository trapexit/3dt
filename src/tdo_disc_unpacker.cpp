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

#include "copy_stream.hpp"
#include "error.hpp"
#include "tdo_directory_header.hpp"
#include "tdo_directory_record.hpp"
#include "tdo_disc_label.hpp"
#include "tdo_disc_unpacker.hpp"

#include <fstream>
#include <cctype>

namespace fs = std::filesystem;

class TDO::DiscUnpacker::Impl final : public TDO::DiscWalker::Callbacks
{
public:
  Impl(std::istream                &is_,
       TDO::DiscUnpacker::Callback &cb_)
    : _cb(cb_),
      _walker(is_,*this)
  {
  }

  ~Impl()
  {
  }

public:
  Error
  unpack()
  {
    return _walker.walk();
  }

public:
  void
  operator()(const std::filesystem::path &path_,
             const TDO::DirectoryRecord &record_,
             TDO::DiscReader            &reader_)
  {
    fs::path fullpath = dstpath / path_;

    _cb.before(path_,record_);
    if(record_.is_directory())
      {
        fs::create_directories(fullpath);
      }
    else
      {
        std::ofstream os;

        reader_.disc_seek(record_.disc_avatar_offset());
        os.open(fullpath,std::ios::binary);
        util::copy_stream(reader_.istream(),os,record_.byte_count);
        os.close();
      }
    _cb.after(path_,record_,0);
  }

public:
  fs::path dstpath;

private:
  TDO::DiscUnpacker::Callback &_cb;
  TDO::DiscWalker              _walker;
};

namespace TDO
{
  DiscUnpacker::DiscUnpacker(std::istream &is_,
                             Callback     &cb_)
  {
    _impl = std::make_unique<Impl>(is_,cb_);
  }

  DiscUnpacker::~DiscUnpacker()
  {
  }

  Error
  DiscUnpacker::unpack(const fs::path &dstpath_)
  {
    fs::create_directories(dstpath_);

    _impl->dstpath = dstpath_;

    return _impl->unpack();
  }
}
