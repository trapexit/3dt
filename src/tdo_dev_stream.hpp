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

#pragma once

#include "error.hpp"
#include "tdo_directory_header.hpp"
#include "tdo_directory_record.hpp"
#include "tdo_disc_label.hpp"
#include "tdo_linked_mem_file_entry.hpp"
#include "tdo_romtag.hpp"

#include "types_ints.h"

#include <iostream>
#include <optional>

namespace TDO
{
  class DevStream
  {
  private:
    u64 _device_block_header;
    u64 _device_block_data_size;
    u64 _device_block_footer;
    u64 _device_block_count;
    u64 _data_start_offset;

  private:
    u64 _disc_label_block;
    u64 _romtags_block;
    std::iostream &_ios;

  public:
    DevStream(std::iostream &ios);

  public:
    void find_label();
    Error setup();

  public:
    bool good() const { return _ios.good(); }
    bool bad() const { return _ios.bad(); }
    bool eof() const { return _ios.eof(); }
    std::iostream &iostream() { return _ios; }

  public:
    u64 data_start_offset() const;

  public:
    u64 device_block_size() const;
    u64 device_block_header() const;
    u64 device_block_data_size() const;
    u64 device_block_footer() const;
    u64 device_block_count() const;

  public:
    void file_seek(const s64 pos);
    void data_byte_seek(const s64 pos);
    void data_byte_skip(const s64 pos);
    void data_block_seek(const s64 pos);
    void data_block_skip(const s64 pos);
    void device_block_seek(const s64 pos);
    void device_block_skip(const s64 pos);

  public:
    s64 file_tell() const;
    s64 data_byte_tell(s64) const;
    s64 data_byte_tell() const;
    s64 data_block_tell(s64) const;
    s64 data_block_tell() const;
    s64 device_block_tell(s64) const;
    s64 device_block_tell() const;

  public:
    s64 file_offset_to_data_block(const s64) const;
    s64 data_block_to_file_offset(const s64) const;

  public:
    TDO::DiscLabel disc_label();
    u64 disc_label_size_in_bytes() const;
    u64 disc_label_block() const;

  public:
    bool has_romtags();
    TDO::ROMTagVec romtags();
    std::optional<TDO::ROMTag> romtag(const int type);
    u64 romtags_block() const;
    u64 romtags_count();
    u64 romtags_size_in_bytes();

  public:
    void read(char *buf, const u64 size);
    void read(char &);
    void read(u8 &);
    void read(u32 &);
    void read(s32 &);
    void read(TDO::DiscLabel &);
    void read(TDO::DirectoryHeader &);
    void read(TDO::DirectoryRecord &);
    void read(TDO::ROMTag &);
    void read(TDO::LinkedMemFileEntry &);
    void read_data_blocks(std::vector<char> &v,
                          const s64          block_pos,
                          const s64          blocks);
    void read_data_bytes_from_block(std::vector<char> &v,
                                    const s64          block_pos,
                                    const s64          bytes);
    void read_data_bytes(std::vector<char> &v,
                         const s64          byte_pos,
                         const s64          bytes);


    template<u64 N>
    void
    read(std::array<char,N> &arr_)
    {
      read(&arr_[0],arr_.size());
    }

    template<u64 N>
    void
    read(std::array<u32,N> &arr_)
    {
      for(u64 i = 0; i < arr_.size(); i++)
        read(arr_[i]);
    }

  public:
    void write(const char *buf, const u64 size);
    void write(char);
    void write(u8);
    void write(u32);
    void write(s32);
    void write(const TDO::DiscLabel &);
    void write(const TDO::DirectoryHeader &);
    void write(const TDO::DirectoryRecord &);
    void write(const TDO::ROMTag &);
    void write(const TDO::LinkedMemFileEntry &);
    void write_data_blocks(const std::vector<char> &v,
                           const s64                block_pos,
                           const s64                blocks);
    void write_data_bytes_from_block(const std::vector<char> &v,
                                     const s64                block_pos,
                                     const s64                bytes);
    void write_data_bytes(const std::vector<char> &v,
                          const s64                byte_pos,
                          const s64                bytes);

    template<u64 N>
    void
    write(std::array<char,N> &arr_)
    {
      write(&arr_[0],arr_.size());
    }

    template<u64 N>
    void
    write(std::array<u32,N> &arr_)
    {
      for(u64 i = 0; i < arr_.size(); i++)
        write(arr_[i]);
    }
  };

  class PosGuard
  {
  public:
    PosGuard(DevStream &stream_)
      : _stream(stream_),
        _pos(_stream.file_tell())
    {
    }

    ~PosGuard()
    {
      _stream.file_seek(_pos);
    }

  private:
    DevStream            &_stream;
    const std::streampos  _pos;
  };
}
