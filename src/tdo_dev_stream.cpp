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

#include "CLI11.hpp"
#include "error_unknown_image_format.hpp"
#include "tdo_dev_stream.hpp"
#include "tdo_disc_label.hpp"
#include "tdo_linked_mem_file_entry.hpp"
#include "types_ints.h"

#include <array>

#include <cctype>
#include <cstdint>
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
swap(u32 &u32_)
{
  u32_ = __builtin_bswap32(u32_);
}

static
inline
void
swap(s32 &s32_)
{
  s32_ = __builtin_bswap32(s32_);
}

static
bool
is_mode1_2352(std::iostream &ios_)
{
  CDROMSectorBuf buf;

  ios_.seekg(0);
  ios_.read((char*)&buf[0],buf.size());

  if(memcmp(&buf[0],&MODE1_SYNC_PATTERN[0],MODE1_SYNC_PATTERN.size()) != 0)
    return false;

  if(buf[0x0F] != 0x01)
    return false;

  return true;
}

TDO::DevStream::DevStream(std::iostream &ios_)
  : _device_block_header(0),
    _device_block_data_size(0),
    _device_block_footer(0),
    _disc_label_block(0),
    _romtags_block(0),
    _data_start_offset(0),
    _ios(ios_)
{
  
}

void
TDO::DevStream::find_label()
{
  int i;
  char v;

  _ios.seekg(0);
  while(_ios && !_ios.eof())
    {
      _ios.read(&v,1);
      if(v != RECORD_STD_VOLUME)
        continue;

      for(i = 0; i < VOLUME_SYNC_BYTE_LEN; i++)
        {
          _ios.read(&v,1);
          if(v == VOLUME_SYNC_BYTE)
            continue;
          break;
        }

      _ios.seekg(-(i + 1),_ios.cur);
      if(i == 5)
        break;
    }
}

Error
TDO::DevStream::setup()
{
  Error err;

  if(::is_mode1_2352(_ios))
    {
      _device_block_header    = 16;
      _device_block_data_size = 2048;
      _device_block_footer    = 288;
    }
  else
    {
      TDO::DiscLabel dl;
      
      find_label();
      if(eof())
        return {"unable to find OperaFS in image"};
      
      _device_block_header    = 0;
      _device_block_data_size = _disc_label.volume_block_size;
      _device_block_footer    = 0;
    }

  find_label();
  _data_start_offset = file_tell();    
  _disc_label_block  = data_block_tell();  
  _romtags_block     = _disc_label_block + 1;      

  // if(_disc_label.num_rom_tags == 0)
  //   _disc_label.num_rom_tags = ::count_m1_romtags(*this,_romtags_block);

  data_block_seek(_romtags_block);
  while(true)
    {
      TDO::ROMTag romtag;

      read(romtag);
      if((romtag.sub_systype == 0) || (romtag.type == 0))
        break;
            
      _romtags.emplace_back(romtag);
    }

  _ios.seekg(0,_ios.end);
  _device_block_count = (_ios.tellg() / device_block_size());
  _ios.seekg(0);
  
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

  dl = stream_.disc_label();

  if(dl.volume_flags != (VOLUME_FLAG_M2 | VOLUME_FLAG_BLESSED))
    return false;
  if(strcmp(&dl.volume_identifier[0],"cd-rom") != 0)
    return false;
  // if(dl.num_rom_tags != 9)
  //   return false;

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

TDO::DiscLabel
TDO::DevStream::disc_label() const
{
  TDO::DiscLabel dl;
  TDO::PosGuard pos_guard(*this);

  data_block_seek(disc_label_block());

  read(dl);
  
  return dl;
}

u64
TDO::DevStream::disc_label_size_in_bytes() const
{
  return sizeof(TDO::DiscLabel);
}

u64
TDO::DevStream::disc_label_block() const
{
  return _disc_label_block;
}

const
TDO::ROMTagVec&
TDO::DevStream::romtags() const
{
  return _romtags;
}

const
std::optional<TDO::ROMTag>
TDO::DevStream::romtag(const int type_) const
{
  for(const auto& romtag : _romtags)
    {
      if(romtag.type != type_)
        continue;

      return romtag;
    }

  return {};
}

u64
TDO::DevStream::romtags_block() const
{
  return _romtags_block;
}

u64
TDO::DevStream::romtags_count() const
{
  return _romtags.size();
}

u64
TDO::DevStream::romtags_size_in_bytes() const
{
  return ((_romtags.size() + 1) * sizeof(TDO::ROMTag));
}

u64
TDO::DevStream::data_start_offset() const
{
  return _data_start_offset;
}

u64
TDO::DevStream::device_block_header() const
{
  return _device_block_header;
}

u64
TDO::DevStream::device_block_data_size() const
{
  return _device_block_data_size;
}

u64
TDO::DevStream::device_block_footer() const
{
  return _device_block_footer;
}

u64
TDO::DevStream::device_block_size() const
{
  return (_device_block_header +
          _device_block_data_size +
          _device_block_footer);
}

u64
TDO::DevStream::device_block_count() const
{
  return _device_block_count;
}

s64
TDO::DevStream::data_block_to_file_offset(s64 data_block_) const
{
  s64 file_offset;

  file_offset = _data_start_offset;
  file_offset += (data_block_ * device_block_size());
  file_offset += _device_block_header;

  return file_offset;
}

s64
TDO::DevStream::file_offset_to_data_block(const s64 file_offset_) const
{
  s64 block;
  s64 extra;
  s64 file_offset;

  file_offset = file_offset_;
  block       = (file_offset / device_block_size());
  extra       = (file_offset % device_block_size());

  file_offset  = (block * _device_block_data_size);
  file_offset += (extra - _device_block_header);
  file_offset -= _data_start_offset;

  return (file_offset / _device_block_data_size);
}

s64
TDO::DevStream::file_tell() const
{
  return _ios.tellg();
}

s64
TDO::DevStream::data_byte_tell(const s64 pos_) const
{
  s64 pos;
  s64 block;
  s64 extra;

  pos   = pos_;
  block = (pos / device_block_size());
  extra = (pos % device_block_size());

  pos  = (block * _device_block_data_size);
  pos += (extra - _device_block_header);
  pos -= _data_start_offset;

  return pos;
}

s64
TDO::DevStream::data_byte_tell() const
{
  s64 pos;

  pos = file_tell();

  return data_byte_tell(pos);
}

s64
TDO::DevStream::data_block_tell(const s64 pos_) const
{
  s64 pos;
  s64 block;
  s64 extra;

  pos   = pos_;
  block = (pos / device_block_size());
  extra = (pos % device_block_size());

  pos  = (block * _device_block_data_size);
  pos += (extra - _device_block_header);
  pos -= _data_start_offset;

  return (pos / _device_block_data_size);
}

s64
TDO::DevStream::data_block_tell() const
{
  s64 pos;

  pos = file_tell();

  return data_block_tell(pos);
}

s64
TDO::DevStream::device_block_tell(const s64 pos_) const
{
  return (pos_ / device_block_size());
}

s64
TDO::DevStream::device_block_tell() const
{
  s64 pos;

  pos = file_tell();

  return device_block_tell(pos);
}

void
TDO::DevStream::file_seek(const s64 pos_)
{
  _ios.seekg(pos_);
}

void
TDO::DevStream::data_byte_seek(const s64 pos_)
{
  s64 pos;
  s64 block;
  s64 extra;

  pos   = (pos_ + _data_start_offset);
  block = (pos / _device_block_data_size);
  extra = (pos % _device_block_data_size);

  pos  = (block * device_block_size());
  pos += _device_block_header;
  pos += extra;

  file_seek(pos);
}

void
TDO::DevStream::data_byte_skip(const s64 count_)
{
  s64 pos;

  pos  = data_byte_tell();
  pos += count_;

  data_byte_seek(pos);
}

void
TDO::DevStream::data_block_seek(const s64 block_)
{
  s64 file_offset;

  file_offset = data_block_to_file_offset(block_);

  return file_seek(file_offset);
}

void
TDO::DevStream::data_block_skip(const s64 count_)
{
  s64 pos;

  pos  = data_block_tell();
  pos += count_;

  data_block_seek(pos);
}

void
TDO::DevStream::device_block_seek(const s64 pos_)
{
  s64 pos;

  pos = (pos_ * device_block_size());

  file_seek(pos);
}

void
TDO::DevStream::device_block_skip(const s64 count_)
{
  s64 pos;

  pos  = device_block_tell();
  pos += count_;

  device_block_seek(pos);
}

void
TDO::DevStream::read(char      *buf_,
                     const u64  size_)
{
  _ios.read(buf_,size_);
}

void
TDO::DevStream::write(const char *buf_,
                      const u64   size_)
{
  _ios.write(buf_,size_);
}

void
TDO::DevStream::read_data_blocks(std::vector<char> &v_,
                                 const s64          pos_,
                                 const s64          blocks_)
{
  size_t end;

  end = v_.size();
  data_block_seek(pos_);
  v_.resize(end + (device_block_data_size() * blocks_));
  for(s64 i = 0; i < blocks_; i++)
    {
      read(&v_[end],device_block_data_size());
      end += device_block_data_size();
    }
}

void
TDO::DevStream::read_data_bytes_from_block(std::vector<char> &v_,
                                           const s64          block_pos_,
                                           const s64          bytes_)
{
  read_data_bytes(v_,
                  (block_pos_ * device_block_data_size()),
                  bytes_);
}

void
TDO::DevStream::read_data_bytes(std::vector<char> &v_,
                                const s64          pos_,
                                const s64          bytes_)
{
  s64 pos;
  s64 vec_end;
  s64 bytes_to_read;
  s64 block_size;

  vec_end = v_.size();
  v_.resize(v_.size() + bytes_);
  block_size = device_block_data_size();

  pos = pos_;
  for(s64 bytes_left = bytes_; bytes_left > 0;)
    {
      data_byte_seek(pos);

      bytes_to_read = (block_size - (pos % block_size));
      bytes_to_read = std::min(bytes_to_read, bytes_left);

      read(&v_[vec_end],bytes_to_read);

      pos        += bytes_to_read;
      bytes_left -= bytes_to_read;
      vec_end    += bytes_to_read;
    }
}

void
TDO::DevStream::read(char &c_)
{
  read(&c_,1);
}

void
TDO::DevStream::write(const char c_)
{
  write(&c_,1);
}

void
TDO::DevStream::read(u8 &u8_)
{
  read((char*)&u8_,1);
}

void
TDO::DevStream::write(u8 u8_)
{
  write((const char*)&u8_,1);
}

void
TDO::DevStream::read(u32 &u32_)
{
  read((char*)&u32_,sizeof(u32));
  swap(u32_);
}

void
TDO::DevStream::write(u32 u32_)
{
  swap(u32_);  
  write((const char*)&u32_,sizeof(u32));
}

void
TDO::DevStream::read(s32 &s32_)
{
  read((char*)&s32_,sizeof(s32));
  swap(s32_);
}

void
TDO::DevStream::write(s32 s32_)
{
  swap(s32_);  
  write((const char*)&s32_,sizeof(s32));
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

  // if(dl_.volume_flags & VOLUME_FLAG_M2)
  //   {
  //     read(dl_.num_rom_tags);
  //     read(dl_.application_id);
  //   }
  // else
  //   {
  //     dl_.num_rom_tags = 0;
  //     dl_.application_id = 0;
  //   }
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
TDO::DevStream::write(const TDO::ROMTag &tag_)
{
  write(tag_.sub_systype);
  write(tag_.type);
  write(tag_.version);
  write(tag_.revision);
  write(tag_.flags);
  write(tag_.type_specific);
  write(tag_.reserved1);
  write(tag_.reserved2);
  write(tag_.offset);
  write(tag_.size);
  write(tag_.reserved3[0]);
  write(tag_.reserved3[1]);
  write(tag_.reserved3[2]);
  write(tag_.reserved3[3]);
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
