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
#include "tdo_linked_mem_file_entry.hpp"

#include <array>

#include <cctype>
#include <cstring>
#include <ios>
#include <cassert>


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

static
std::uint32_t
count_m1_romtags(TDO::DevStream     &stream_,
                 const std::int64_t  pos_)
{
  uint32_t count;
  TDO::ROMTag tag;
  TDO::PosGuard guard(stream_);

  stream_.data_block_seek(pos_);

  count = 0;
  while(true)
    {
      stream_.read(tag);
      if((tag.sub_systype == 0) || (tag.type == 0))
        break;
      count++;
    }

  return count;
}

TDO::DevStream::DevStream(std::istream &is_)
  : _device_block_data_size(0),
    _device_block_header(0),
    _device_block_footer(0),
    _data_offset(0),
    _is(is_),
    _initialized(false)
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
      if(v != RECORD_STD_VOLUME)
        continue;

      for(i = 0; i < VOLUME_SYNC_BYTE_LEN; i++)
        {
          _is.read(&v,1);
          if(v == VOLUME_SYNC_BYTE)
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
  if(eof())
    return {"unable to find OperaFS in image"};

  {
    PosGuard guard(*this);
    read(label);
    _device_block_data_size = label.volume_block_size;
  }

  _initialized = true;

  _data_offset = data_byte_tell();

  return {};
}

static
bool
is_romfs(TDO::DevStream &stream_)
{
  return ((stream_.device_block_header()    == 0) &&
          (stream_.device_block_data_size() == 4) &&
          (stream_.device_block_footer()    == 0));
}

static
bool
is_konami_m2(TDO::DevStream &stream_)
{
  TDO::DiscLabel dl;

  stream_.find_label();
  if(stream_.eof())
    return false;
  stream_.read(dl);

  if(dl.volume_flags != (VOLUME_FLAG_M2 | VOLUME_FLAG_BLESSED))
    return false;
  if(strcmp(&dl.volume_identifier[0],"cd-rom") != 0)
    return false;
  if(dl.num_rom_tags != 9)
    return false;

  return true;
}

bool
TDO::DevStream::has_romtags()
{
  TDO::PosGuard pos_guard(*this);

  if(::is_romfs(*this))
    return false;
  if(::is_konami_m2(*this))
    return false;

  return true;
}

TDO::ROMTagVec
TDO::DevStream::romtags()
{
  std::int64_t pos;
  TDO::DiscLabel dl;
  TDO::ROMTagVec tags;

  if(!has_romtags())
    return tags;

  find_label();
  pos = data_block_tell();
  read(dl);

  if(dl.num_rom_tags == 0)
    dl.num_rom_tags = ::count_m1_romtags(*this,pos+1);

  data_block_seek(pos+1);
  for(uint32_t i = 0; i < dl.num_rom_tags; i++)
    {
      TDO::ROMTag tag;
      read(tag);
      tags.push_back(tag);
    }

  return tags;
}

std::uint32_t
TDO::DevStream::data_offset() const
{
  return _data_offset;
}

std::uint32_t
TDO::DevStream::device_block_header() const
{
  return _device_block_header;
}

std::uint32_t
TDO::DevStream::device_block_data_size() const
{
  return _device_block_data_size;
}

std::uint32_t
TDO::DevStream::device_block_footer() const
{
  return _device_block_footer;
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
  assert(_initialized == true);

  PosGuard guard(*this);
  std::uint64_t pos;

  _is.seekg(0,_is.end);
  pos = _is.tellg();
  pos /= device_block_size();

  return pos;
}

std::int64_t
TDO::DevStream::data_block_to_file_offset(std::int64_t data_block_) const
{
  assert(_initialized == true);

  std::int64_t file_offset;

  file_offset = _data_offset;
  file_offset += (data_block_ * device_block_size());
  file_offset += _device_block_header;

  return file_offset;
}

std::int64_t
TDO::DevStream::file_offset_to_data_block(const std::int64_t file_offset_) const
{
  assert(_initialized == true);

  std::int64_t block;
  std::int64_t extra;
  std::int64_t file_offset;

  file_offset = file_offset_;
  block       = (file_offset / device_block_size());
  extra       = (file_offset % device_block_size());

  file_offset  = (block * _device_block_data_size);
  file_offset += (extra - _device_block_header);
  file_offset -= _data_offset;

  return (file_offset / _device_block_data_size);
}

std::int64_t
TDO::DevStream::file_tell() const
{
  return _is.tellg();
}

std::int64_t
TDO::DevStream::data_byte_tell(const std::int64_t pos_) const
{
  assert(_initialized == true);

  std::int64_t pos;
  std::int64_t block;
  std::int64_t extra;

  pos   = pos_;
  block = (pos / device_block_size());
  extra = (pos % device_block_size());

  pos  = (block * _device_block_data_size);
  pos += (extra - _device_block_header);
  pos -= _data_offset;

  return pos;
}

std::int64_t
TDO::DevStream::data_byte_tell() const
{
  std::int64_t pos;

  pos = file_tell();

  return data_byte_tell(pos);
}

std::int64_t
TDO::DevStream::data_block_tell(const std::int64_t pos_) const
{
  assert(_initialized == true);

  std::int64_t pos;
  std::int64_t block;
  std::int64_t extra;

  pos   = pos_;
  block = (pos / device_block_size());
  extra = (pos % device_block_size());

  pos  = (block * _device_block_data_size);
  pos += (extra - _device_block_header);
  pos -= _data_offset;

  return (pos / _device_block_data_size);
}

std::int64_t
TDO::DevStream::data_block_tell() const
{
  std::int64_t pos;

  pos = file_tell();

  return data_block_tell(pos);
}

std::int64_t
TDO::DevStream::device_block_tell(const std::int64_t pos_) const
{
  assert(_initialized == true);

  return (pos_ / device_block_size());
}

std::int64_t
TDO::DevStream::device_block_tell() const
{
  std::int64_t pos;

  pos = file_tell();

  return device_block_tell(pos);
}

void
TDO::DevStream::file_seek(const std::int64_t pos_)
{
  _is.seekg(pos_);
}

void
TDO::DevStream::data_byte_seek(const std::int64_t pos_)
{
  assert(_initialized == true);

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
TDO::DevStream::data_block_seek(const std::int64_t block_)
{
  std::int64_t file_offset;

  file_offset = data_block_to_file_offset(block_);

  return file_seek(file_offset);
}

void
TDO::DevStream::data_block_skip(const std::int64_t count_)
{
  std::int64_t pos;

  pos  = data_block_tell();
  pos += count_;

  data_block_seek(pos);
}

void
TDO::DevStream::device_block_seek(const std::int64_t pos_)
{
  std::int64_t pos;

  pos = (pos_ * device_block_size());

  file_seek(pos);
}

void
TDO::DevStream::device_block_skip(const std::int64_t count_)
{
  std::int64_t pos;

  pos  = device_block_tell();
  pos += count_;

  device_block_seek(pos);
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
TDO::DevStream::read(uint8_t &u8_)
{
  read((char*)&u8_,1);
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
TDO::DevStream::read(TDO::DiscLabel &dl_)
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

  if(dl_.volume_flags & VOLUME_FLAG_M2)
    {
      read(dl_.num_rom_tags);
      read(dl_.application_id);
    }
  else
    {
      dl_.num_rom_tags = 0;
      dl_.application_id = 0;
    }
}

void
TDO::DevStream::read(TDO::DirectoryHeader &dh_)
{
  read(dh_.next_block);
  read(dh_.prev_block);
  read(dh_.flags);
  read(dh_.first_free_byte);
  read(dh_.first_entry_offset);
}

void
TDO::DevStream::read(TDO::DirectoryRecord &dr_)
{
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

void
TDO::DevStream::read(TDO::ROMTag &tag_)
{
  read(tag_.sub_systype);
  read(tag_.type);
  read(tag_.version);
  read(tag_.revision);
  read(tag_.flags);
  read(tag_.type_specific);
  read(tag_.reserved1);
  read(tag_.reserved2);
  read(tag_.offset);
  read(tag_.size);
  read(tag_.reserved3[0]);
  read(tag_.reserved3[1]);
  read(tag_.reserved3[2]);
  read(tag_.reserved3[3]);
}

void
TDO::DevStream::read(TDO::LinkedMemFileEntry &lmfe_)
{
  read(lmfe_.fingerprint);
  read(lmfe_.flink_offset);
  read(lmfe_.blink_offset);
  read(lmfe_.block_count);
  read(lmfe_.header_block_count);
  read(lmfe_.byte_count);
  read(lmfe_.unique_identifier);
  read(lmfe_.type);
  read(lmfe_.filename,sizeof(lmfe_.filename));
}
