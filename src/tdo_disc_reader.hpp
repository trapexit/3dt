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

#include <iostream>
#include <stack>


namespace TDO
{
  class DiscReader
  {
  public:
    DiscReader(std::istream &is);

  public:
    Error discover_image_format();
    uint32_t sector_size() const { return _sector_size; }
    uint32_t sector_data_offset() const { return _sector_data_offset; }

  public:
    bool good() const { return _is.good(); }
    bool bad() const { return _is.bad(); }
    bool eof() const { return _is.eof(); }
    std::istream &istream() { return _is; }

  public:
    void stream_seek(const std::int64_t pos);
    void disc_seek(const std::int64_t pos);
    void sector_seek(const std::int64_t sector);
    std::int64_t disc_tell() const;
    std::int64_t stream_tell() const;

  public:
    std::int64_t sector_count() const;

  public:
    void read(char *buf, uint32_t size);
    void read(char &c);
    void read(uint32_t &u32);
    void read(int32_t &i32);
    void read(TDO::DiscLabel &);
    void read(TDO::DirectoryHeader &);
    void read(TDO::DirectoryRecord &);

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
    void push_pos();
    void pop_pos();

  private:
    typedef std::stack<std::size_t> PosStack;

    std::uint32_t  _sector_size;
    std::uint32_t  _sector_data_offset;
    std::istream  &_is;
    PosStack       _pos_stack;
  };

  class PosGuard
  {
  public:
    PosGuard(DiscReader &reader_)
      : _reader(reader_)
    {
      _reader.push_pos();
    }

    ~PosGuard()
    {
      _reader.pop_pos();
    }

  private:
    DiscReader &_reader;
  };
}
