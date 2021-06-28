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

#include "tdo_disc_file_reader.hpp"

namespace TDO
{
  DiscFileReader::DiscFileReader()
    : DiscReader(_ifs)
  {

  }

  DiscFileReader::~DiscFileReader()
  {
    close();
  }

  void
  DiscFileReader::open(const std::filesystem::path &filepath_)
  {
    close();

    _ifs.open(filepath_,std::ios::binary);

    _filepath = filepath_;
  }

  const
  std::filesystem::path&
  DiscFileReader::filepath() const
  {
    return _filepath;
  }

  std::istream&
  DiscFileReader::istream()
  {
    return _ifs;
  }

  void
  DiscFileReader::close()
  {
    if(_ifs.is_open())
      _ifs.close();
  }
}
