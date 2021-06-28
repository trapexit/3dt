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
#include "tdo_disc_reader.hpp"

#include <string.h>

#include <array>

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
is_3do_iso(const uint8_t *buf_)
{
  if(buf_[0] != 0x01)
    return false;

  if(memcmp(&buf_[1],&VOLUME_SYNC_BYTES[0],VOLUME_SYNC_BYTES.size()) != 0)
    return false;

  if(buf_[6] != 0x01)
    return false;

  if(buf_[7] != 0x00)
    return false;

  return true;
}

static
bool
is_mode1_2352(const uint8_t *buf_)
{
  if(memcmp(&buf_[0],&MODE1_SYNC_PATTERN[0],MODE1_SYNC_PATTERN.size()) != 0)
    return false;

  if(buf_[0x0F] != 0x01)
    return false;

  return ::is_3do_iso(&buf_[0x10]);
}

namespace TDO
{
  DiscReader::DiscReader(std::istream &is_)
    : _sector_size(0),
      _sector_data_offset(0),
      _is(is_)
  {

  }

  void
  DiscReader::stream_seek(const std::int64_t pos_)
  {
    _is.seekg(pos_);
  }

  void
  DiscReader::disc_seek(const std::int64_t tdo_pos_)
  {
    std::int64_t tdo_sector;
    std::int64_t tdo_data_offset;

    tdo_data_offset = (tdo_pos_ % TDO_SECTOR_SIZE);
    tdo_sector      = (tdo_pos_ / TDO_SECTOR_SIZE);

    stream_seek(_sector_data_offset +
                tdo_data_offset +
                (tdo_sector * _sector_size));
  }

  void
  DiscReader::sector_seek(const std::int64_t sector_)
  {
    disc_seek(sector_ * TDO_SECTOR_SIZE);
  }

  std::int64_t
  DiscReader::disc_tell() const
  {
    std::int64_t img_offset;
    std::int64_t img_sector;
    std::int64_t img_data_offset;

    img_offset      = _is.tellg();
    img_sector      = (img_offset / _sector_size);
    img_data_offset = (img_offset % _sector_size);

    return ((img_sector * TDO_SECTOR_SIZE) +
            img_data_offset -
            _sector_data_offset);
  }

  std::int64_t
  DiscReader::stream_tell() const
  {
    return _is.tellg();
  }

  std::int64_t
  DiscReader::sector_count() const
  {
    std::int64_t rv;
    std::int64_t offset;

    offset = _is.tellg();

    _is.seekg(0,std::ios::end);

    rv = (_is.tellg() / _sector_size);

    _is.seekg(offset);

    return rv;
  }

  Error
  DiscReader::discover_image_format()
  {
    CDROMSectorBuf buf;

    _is.seekg(0);
    _is.read((char*)&buf[0],buf.size());

    if(::is_mode1_2352(&buf[0]))
      {
        _sector_size        = 2352;
        _sector_data_offset = 16;
      }
    else if(::is_3do_iso(&buf[0]))
      {
        _sector_size        = 2048;
        _sector_data_offset = 0;
      }
    else
      {
        return {"not a CDROM image or unsupported format"};
      }

    return {};
  }

  void
  DiscReader::read(char     *buf_,
                   uint32_t  size_)
  {
    _is.read(buf_,size_);
  }

  void
  DiscReader::read(char &c_)
  {
    read(&c_,1);
  }

  void
  DiscReader::read(uint32_t &u32_)
  {
    read((char*)&u32_,sizeof(uint32_t));
    swap(u32_);
  }

  void
  DiscReader::read(int32_t &i32_)
  {
    read((char*)&i32_,sizeof(int32_t));
    swap(i32_);
  }

  void
  DiscReader::read(DiscLabel &dl_)
  {
    dl_.file_offset = stream_tell();
    dl_.disc_offset = disc_tell();

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
  DiscReader::read(DirectoryHeader &dh_)
  {
    dh_.file_offset = stream_tell();
    dh_.disc_offset = disc_tell();

    read(dh_.next_block);
    read(dh_.prev_block);
    read(dh_.flags);
    read(dh_.first_free_byte);
    read(dh_.first_entry_offset);
  }

  void
  DiscReader::read(DirectoryRecord &dr_)
  {
    dr_.file_offset = stream_tell();
    dr_.disc_offset = disc_tell();

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
  DiscReader::push_pos()
  {
    _pos_stack.push(_is.tellg());
  }

  void
  DiscReader::pop_pos()
  {
    _is.seekg(_pos_stack.top());
    _pos_stack.pop();
  }
}
