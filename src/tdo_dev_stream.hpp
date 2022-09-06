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

#pragma once

#include "error.hpp"
#include "tdo_directory_header.hpp"
#include "tdo_directory_record.hpp"
#include "tdo_disc_label.hpp"
#include "tdo_romtag.hpp"

#include <iostream>
#include <stack>

namespace TDO
{
  class DevStream
  {
  public:
    DevStream(std::istream &is);

  public:
    void find_label();
    Error setup();

  public:
    bool good() const { return _is.good(); }
    bool bad() const { return _is.bad(); }
    bool eof() const { return _is.eof(); }
    std::istream &istream() { return _is; }

  public:
    std::uint32_t data_offset() const;
    std::uint32_t device_block_size() const;
    std::uint32_t device_block_header() const;
    std::uint32_t device_block_footer() const;

    std::uint32_t device_block_count();
    std::uint32_t data_block_size() const;

  public:
    void file_seek(const std::int64_t pos);
    void data_byte_seek(const std::int64_t pos);
    void data_byte_skip(const std::int64_t pos);
    void data_block_seek(const std::int64_t pos);
    void device_block_seek(const std::int64_t pos);

  public:
    std::int64_t file_tell() const;
    std::int64_t data_byte_tell(std::int64_t) const;
    std::int64_t data_byte_tell() const;
    std::int64_t data_block_tell(std::int64_t) const;
    std::int64_t data_block_tell() const;

  public:
    void read(char *buf, uint32_t size);
    void read(char &c);
    void read(uint8_t &u8);
    void read(uint32_t &u32);
    void read(int32_t &i32);
    void read(TDO::DiscLabel &);
    void read(TDO::DirectoryHeader &);
    void read(TDO::DirectoryRecord &);
    void read(TDO::ROMTag &);

    template<std::size_t N>
    void
    read(std::array<char,N> &arr_)
    {
      read(&arr_[0],arr_.size());
    }

    template<std::size_t N>
    void
    read(std::array<uint32_t,N> &arr_)
    {
      for(std::size_t i = 0; i < arr_.size(); i++)
        read(arr_[i]);
    }

  public:
    bool is_romfs() const;

  private:
    std::uint32_t  _device_block_data_size;
    std::uint32_t  _device_block_header;
    std::uint32_t  _device_block_footer;
    std::uint32_t  _data_offset;
    std::istream  &_is;
    bool           _initialized;
  };

  class PosGuard
  {
  public:
    PosGuard(DevStream &stream_)
      : _stream(stream_)
    {
      _pos = _stream.file_tell();
    }

    ~PosGuard()
    {
      _stream.file_seek(_pos);
    }

  private:
    DevStream      &_stream;
    std::streampos  _pos;
  };
}
