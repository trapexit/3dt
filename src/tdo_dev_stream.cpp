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

#include "error_unknown_image_format.hpp"
#include "tdo_dev_stream.hpp"

#include <array>

#include <cctype>
#include <cstring>
#include <ios>


static constexpr int TDO_SECTOR_SIZE   = 2048;
static constexpr int CDROM_SECTOR_SIZE = 2352;
static constexpr int SYNC_PATTERN_SIZE = 12;
typedef std::array<uint8_t,CDROM_SECTOR_SIZE> CDROMSectorBuf;
typedef std::array<uint8_t,SYNC_PATTERN_SIZE> CDROMSyncPatternBuf;
typedef std::array<uint8_t,VOLUME_SYNC_BYTE_LEN> VolumeSyncByteBuf;
static constexpr CDROMSyncPatternBuf MODE1_SYNC_PATTERN = {0x00,0xFF,0xFF,0xFF,
  0xFF,0xFF,0xFF,0xFF,
  0xFF,0xFF,0xFF,0x00};
static constexpr VolumeSyncByteBuf VOLUME_SYNC_BYTES = {0x5A,0x5A,0x5A,0x5A,0x5A};
static constexpr std::array<uint8_t,4> ARM_NOOP = {0xE1,0xA0,0x10,01};

static
inline
void
swap(uint32_t &u32_)
{
  u32_ = __builtin_bswap32(u32_);
}

static
inline
void
swap(int32_t &i32_)
{
  i32_ = __builtin_bswap32(i32_);
}

static
bool
is_mode1_2352(std::istream &is_)
{
  CDROMSectorBuf buf;

  is_.seekg(0);
  is_.read((char*)&buf[0],buf.size());

  if(memcmp(&buf[0],&MODE1_SYNC_PATTERN[0],MODE1_SYNC_PATTERN.size()) != 0)
    return false;

  if(buf[0x0F] != 0x01)
    return false;

  return true;
}

TDO::DevStream::DevStream(std::istream &is_)
  : _device_block_data_size(0),
    _device_block_header(0),
    _device_block_footer(0),
    _data_offset(0),
    _is(is_)
{
}

void
TDO::DevStream::find_label()
{
  int i;
  char v;

  _is.seekg(0);
  while(_is && !_is.eof())
    {
      _is.read(&v,1);
      if(v != 0x01)
        continue;

      for(i = 0; i < 5; i++)
        {
          _is.read(&v,1);
          if(v == 0x5A)
            continue;
          break;
        }

      _is.seekg(-(i + 1),_is.cur);
      if(i == 5)
        break;
    }
}

Error
TDO::DevStream::setup()
{
  Error err;
  TDO::DiscLabel label;

  if(::is_mode1_2352(_is))
    {
      _device_block_header = 16;
      _device_block_footer = 288;
    }

  find_label();
  if(_is.eof())
    return {"unable to find OperaFS in image"};

  {
    PosGuard guard(*this);
    _read(label);
    _device_block_data_size = label.volume_block_size;
  }

  _data_offset = data_byte_tell();

  return {};
}

std::uint32_t
TDO::DevStream::device_block_size() const
{
  return (_device_block_header +
          _device_block_data_size +
          _device_block_footer);
}

std::uint32_t
TDO::DevStream::device_block_count()
{
  PosGuard guard(*this);
  std::uint64_t pos;

  _is.seekg(0,_is.end);
  pos = _is.tellg();
  pos /= device_block_size();

  return pos;
}

std::int64_t
TDO::DevStream::file_tell() const
{
  return _is.tellg();
}

std::int64_t
TDO::DevStream::data_byte_tell() const
{
  std::int64_t pos;
  std::int64_t block;
  std::int64_t extra;

  pos   = file_tell();
  block = (pos / device_block_size());
  extra = (pos % device_block_size());

  pos  = (block * _device_block_data_size);
  pos += (extra - _device_block_header);

  return pos;
}

void
TDO::DevStream::file_seek(const std::int64_t pos_)
{
  _is.seekg(pos_);
}

void
TDO::DevStream::data_byte_seek(const std::int64_t pos_)
{
  std::int64_t pos;
  std::int64_t block;
  std::int64_t extra;

  pos   = (pos_ + _data_offset);
  block = (pos / _device_block_data_size);
  extra = (pos % _device_block_data_size);

  pos  = (block * device_block_size());
  pos += _device_block_header;
  pos += extra;

  file_seek(pos);
}

void
TDO::DevStream::data_byte_skip(const std::int64_t count_)
{
  std::int64_t pos;

  pos  = data_byte_tell();
  pos += count_;

  data_byte_seek(pos);
}

void
TDO::DevStream::data_block_seek(const std::int64_t pos_)
{
  std::int64_t pos;

  pos = (pos_ * _device_block_data_size);

  return data_byte_seek(pos);
}

void
TDO::DevStream::device_block_seek(const std::int64_t pos_)
{
  std::int64_t pos;

  pos = (pos_ * device_block_size());

  file_seek(pos);
}

void
TDO::DevStream::read(char     *buf_,
                     uint32_t  size_)
{
  _is.read(buf_,size_);
}

void
TDO::DevStream::read(char &c_)
{
  read(&c_,1);
}

void
TDO::DevStream::read(uint32_t &u32_)
{
  read((char*)&u32_,sizeof(uint32_t));
  swap(u32_);
}

void
TDO::DevStream::read(int32_t &i32_)
{
  read((char*)&i32_,sizeof(int32_t));
  swap(i32_);
}

void
TDO::DevStream::_read(TDO::DiscLabel &dl_)
{
  read(dl_.record_type);
  read(dl_.volume_sync_bytes);
  read(dl_.volume_structure_version);
  read(dl_.volume_flags);
  read(dl_.volume_commentary);
  read(dl_.volume_identifier);
  read(dl_.volume_unique_identifier);
  read(dl_.volume_block_size);
  read(dl_.volume_block_count);
  read(dl_.root_unique_identifier);
  read(dl_.root_directory_block_count);
  read(dl_.root_directory_block_size);
  read(dl_.root_directory_last_avatar_index);
  read(dl_.root_directory_avatar_list);
}

void
TDO::DevStream::read(TDO::DiscLabel &dl_)
{
  dl_.file_offset = file_tell();
  dl_.data_offset = data_byte_tell();

  _read(dl_);
}

void
TDO::DevStream::read(TDO::DirectoryHeader &dh_)
{
  dh_.file_offset = file_tell();
  dh_.data_offset = data_byte_tell();

  read(dh_.next_block);
  read(dh_.prev_block);
  read(dh_.flags);
  read(dh_.first_free_byte);
  read(dh_.first_entry_offset);
}

void
TDO::DevStream::read(TDO::DirectoryRecord &dr_)
{
  dr_.file_offset = file_tell();
  dr_.data_offset = data_byte_tell();

  read(dr_.flags);
  read(dr_.unique_identifier);
  read(dr_.type);
  read(dr_.block_size);
  read(dr_.byte_count);
  read(dr_.block_count);
  read(dr_.burst);
  read(dr_.gap);
  read(dr_.filename,32);
  read(dr_.last_avatar_index);
  if(dr_.last_avatar_index >= 7)
    dr_.last_avatar_index = 7;

  dr_.avatar_list.clear();
  for(uint32_t i = 0; i <= dr_.last_avatar_index; i++)
    {
      uint32_t tmp;
      read(tmp);
      dr_.avatar_list.push_back(tmp);
    }
}
