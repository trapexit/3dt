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
#include "fmt.hpp"
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
static constexpr std::uint32_t MAX_DIRECTORY_AVATAR_INDEX = ROOT_HIGHEST_AVATAR;
static constexpr std::uint32_t DIRECTORY_HEADER_SIZE = sizeof(TDO::DirectoryHeader);
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
_swap(uint32_t &u32_)
{
  u32_ = __builtin_bswap32(u32_);
}

static
inline
void
_swap(int32_t &i32_)
{
  i32_ = __builtin_bswap32(i32_);
}

static
inline
void
_swap(uint16_t &u16_)
{
  u16_ = __builtin_bswap16(u16_);
}

static
inline
void
_swap(int16_t &i16_)
{
  i16_ = __builtin_bswap16(i16_);
}

static
inline
void
_swap(uint64_t &u64_)
{
  u64_ = __builtin_bswap64(u64_);
}

static
inline
void
_swap(int64_t &i64_)
{
  i64_ = __builtin_bswap64(i64_);
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

bool
TDO::DevStream::is_mode1_2352()
{
  CDROMSectorBuf buf;

  _is.seekg(0);
  _is.read((char*)&buf[0],buf.size());

  const bool has_mode1_sync_pattern =
    (memcmp(&buf[0],&MODE1_SYNC_PATTERN[0],MODE1_SYNC_PATTERN.size()) == 0);
  const bool has_mode1_sector_marker = (buf[0x0F] == 0x01);

  return (has_mode1_sync_pattern && has_mode1_sector_marker);
}

std::uint32_t
TDO::DevStream::count_m1_romtags(const std::int64_t pos_)
{
  uint32_t count;
  TDO::ROMTag tag;
  TDO::PosGuard guard(*this);

  data_block_seek(pos_);

  count = 0;
  while(true)
    {
      read(tag);
      if((tag.sub_systype == 0) || (tag.type == 0))
        break;
      count++;
    }

  return count;
}

void
TDO::DevStream::setup()
{
  TDO::DiscLabel label;

  if(is_mode1_2352())
    {
      _device_block_header = 16;
      _device_block_footer = 288;
    }

  find_label();
  if(eof())
    _throw("unable to find OperaFS in image");

  {
    PosGuard guard(*this);
    read(label);
    _device_block_data_size = label.volume_block_size;
  }

  _initialized = true;

  _data_offset = data_byte_tell();
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
  const bool found_label = !stream_.eof();

  if(!found_label)
    return found_label;
  stream_.read(dl);

  const bool has_expected_volume_flags =
    (dl.volume_flags == (VOLUME_FLAG_M2 | VOLUME_FLAG_BLESSED));
  const bool has_expected_volume_identifier =
    (strcmp(&dl.volume_identifier[0],"cd-rom") == 0);
  const bool has_expected_rom_tag_count = (dl.num_rom_tags == 9);

  return (has_expected_volume_flags &&
          has_expected_volume_identifier &&
          has_expected_rom_tag_count);
}

bool
TDO::DevStream::has_romtags()
{
  TDO::PosGuard pos_guard(*this);

  const bool uses_romfs_layout = ::is_romfs(*this);
  const bool uses_konami_m2_layout = ::is_konami_m2(*this);

  return (!uses_romfs_layout && !uses_konami_m2_layout);
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
    dl.num_rom_tags = count_m1_romtags(pos+1);

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
  const std::streampos pos = _is.tellg();

  if(!_is.good())
    _throw("bad stream state before read");

  _is.read(buf_,size_);

  if(!_is.good())
    _throw("bad stream state after read");
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
  _swap(u32_);
}

void
TDO::DevStream::read(int32_t &i32_)
{
  read((char*)&i32_,sizeof(int32_t));
  _swap(i32_);
}

void
TDO::DevStream::read(TDO::DiscLabel &dl_)
{
  TDO::DiscLabel tmp;

  read(&tmp.record_type,sizeof(tmp.record_type));
  read(&tmp.volume_sync_bytes[0],sizeof(tmp.volume_sync_bytes));
  read(&tmp.volume_structure_version,sizeof(tmp.volume_structure_version));
  read(&tmp.volume_flags,sizeof(tmp.volume_flags));
  read(&tmp.volume_commentary[0],sizeof(tmp.volume_commentary));
  read(&tmp.volume_identifier[0],sizeof(tmp.volume_identifier));
  read(tmp.volume_unique_identifier);
  read(tmp.volume_block_size);
  read(tmp.volume_block_count);
  read(tmp.root_unique_identifier);
  read(tmp.root_directory_block_count);
  read(tmp.root_directory_block_size);
  read(tmp.root_directory_last_avatar_index);

  for(std::size_t i = 0; i < tmp.root_directory_avatar_list.size(); i++)
    read(tmp.root_directory_avatar_list[i]);

  if(tmp.volume_flags & VOLUME_FLAG_M2)
    {
      read(tmp.num_rom_tags);
      read(tmp.application_id);
      memset(tmp.reserved,0,sizeof(tmp.reserved));
    }
  else
    {
      tmp.num_rom_tags = 0;
      tmp.application_id = 0;
    }

  if(tmp.record_type != RECORD_STD_VOLUME)
    _throw("invalid OperaFS disc label: incorrect record type");
  if(memcmp(&tmp.volume_sync_bytes[0],&VOLUME_SYNC_BYTES[0],VOLUME_SYNC_BYTE_LEN) != 0)
    _throw("invalid OperaFS disc label: incorrect volume sync bytes");
  if((tmp.volume_structure_version != VOLUME_STRUCTURE_OPERA_READONLY) &&
     (tmp.volume_structure_version != VOLUME_STRUCTURE_LINKED_MEM) &&
     (tmp.volume_structure_version != VOLUME_STRUCTURE_ACROBAT))
    _throw("invalid OperaFS disc label: unknown volume structure version");
  if(tmp.volume_block_size == 0)
    _throw("invalid OperaFS disc label: zero volume block size");
  if((tmp.volume_structure_version != VOLUME_STRUCTURE_LINKED_MEM) &&
     (tmp.volume_block_size != 4) &&
     (tmp.volume_block_size % TDO_SECTOR_SIZE != 0))
    _throw("invalid OperaFS disc label: volume block size not sector aligned");
  if(tmp.root_directory_last_avatar_index > ROOT_HIGHEST_AVATAR)
    _throw("invalid OperaFS disc label: root directory avatar index exceeds maximum");

  dl_ = tmp;
}

void
TDO::DevStream::read(TDO::DirectoryHeader &dh_)
{
  TDO::DirectoryHeader tmp;

  read(tmp.next_block);
  read(tmp.prev_block);
  read(tmp.flags);
  read(tmp.first_free_byte);
  read(tmp.first_entry_offset);

  const std::uint32_t dev_block_count = device_block_count();

  if(tmp.next_block < -1)
    _throw("unsafe OperaFS directory metadata: invalid next_block");
  if(tmp.prev_block < -1)
    _throw("unsafe OperaFS directory metadata: invalid prev_block");
  if((tmp.next_block != -1) &&
     (static_cast<std::uint32_t>(tmp.next_block) >= dev_block_count))
    _throw("unsafe OperaFS directory metadata: next_block exceeds device bounds");
  if((tmp.prev_block != -1) &&
     (static_cast<std::uint32_t>(tmp.prev_block) >= dev_block_count))
    _throw("unsafe OperaFS directory metadata: prev_block exceeds device bounds");
  if(tmp.first_free_byte < DIRECTORY_HEADER_SIZE)
    _throw("unsafe OperaFS directory metadata: first_free_byte overlaps header");
  if(tmp.first_entry_offset < DIRECTORY_HEADER_SIZE)
    _throw("unsafe OperaFS directory metadata: first_entry_offset overlaps header");
  if(tmp.first_entry_offset > tmp.first_free_byte)
    _throw("unsafe OperaFS directory metadata: first_entry_offset exceeds first_free_byte");

  dh_ = tmp;
}

void
TDO::DevStream::read(TDO::DirectoryRecord &dr_)
{
  TDO::DirectoryRecord tmp;
  std::array<std::uint32_t, MAX_DIRECTORY_AVATAR_INDEX + 1> avatar_list;

  read(tmp.flags);
  read(tmp.unique_identifier);
  read(tmp.type);
  read(tmp.block_size);
  read(tmp.byte_count);
  read(tmp.block_count);
  read(tmp.burst);
  read(tmp.gap);
  read(tmp.filename,sizeof(tmp.filename));
  read(tmp.last_avatar_index);

  if(tmp.last_avatar_index > MAX_DIRECTORY_AVATAR_INDEX)
    _throw("impossible OperaFS directory record last_avatar_index: %d > %d",
           tmp.last_avatar_index, MAX_DIRECTORY_AVATAR_INDEX);

  for(std::uint32_t i = 0; i <= tmp.last_avatar_index; i++)
    read(avatar_list[i]);

  tmp.avatar_list.assign(avatar_list.begin(),
                         avatar_list.begin() + (tmp.last_avatar_index + 1));

  const std::uint32_t avatar_count = tmp.last_avatar_index + 1;
  const std::uint32_t data_block_size = device_block_data_size();

  if(tmp.last_avatar_index > MAX_DIRECTORY_AVATAR_INDEX)
    _throw("impossible OperaFS directory record last_avatar_index: {} > {}",
           tmp.last_avatar_index,MAX_DIRECTORY_AVATAR_INDEX);
  if(tmp.avatar_list.size() != avatar_count)
    _throw("impossible OperaFS directory record avatar count: avatar_list.size()={} last_avatar_index+1={}",
           tmp.avatar_list.size(),avatar_count);
  if(tmp.block_size == 0)
    _throw("unsafe OperaFS directory record metadata: zero block_size");
  if((data_block_size == 0) || ((tmp.block_size % data_block_size) != 0))
    _throw("unsafe OperaFS directory record metadata: invalid block_size alignment");

  const std::uint32_t dev_block_count = device_block_count();
  const std::uint64_t max_byte_count =
    static_cast<std::uint64_t>(tmp.block_size) * static_cast<std::uint64_t>(tmp.block_count);

  if((tmp.block_count == 0) && (avatar_count > 1))
    _throw("unsafe OperaFS directory record metadata: avatars without blocks");
  if((tmp.byte_count != 0) && (tmp.block_count == 0))
    _throw("unsafe OperaFS directory record metadata: byte_count without blocks");
  if((tmp.block_count != 0) && (tmp.byte_count > max_byte_count))
    _throw("unsafe OperaFS directory record metadata: byte_count exceeds block capacity");

  if(tmp.block_count != 0)
    for(std::uint32_t avatar : tmp.avatar_list)
      {
        if(avatar >= dev_block_count)
          _throw("unsafe OperaFS directory record metadata: avatar exceeds device bounds");
      }

  dr_ = tmp;
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
  TDO::LinkedMemFileEntry tmp;

  read(tmp.fingerprint);
  read(tmp.flink_offset);
  read(tmp.blink_offset);
  read(tmp.block_count);
  read(tmp.header_block_count);
  read(tmp.byte_count);
  read(tmp.unique_identifier);
  read(tmp.type);
  read(tmp.filename, sizeof(tmp.filename));

  if((tmp.fingerprint != FINGERPRINT_FILEBLOCK) &&
     (tmp.fingerprint != FINGERPRINT_FREEBLOCK) &&
     (tmp.fingerprint != FINGERPRINT_ANCHORBLOCK))
    _throw("invalid OperaFS linked mem file entry: {}",
           "unknown fingerprint");
  if(tmp.flink_offset < sizeof(TDO::LinkedMemFileEntry))
    _throw("invalid OperaFS linked mem file entry: {}",
           "flink offset too small");
  if(tmp.blink_offset < sizeof(TDO::LinkedMemFileEntry))
    _throw("invalid OperaFS linked mem file entry: {}",
           "blink offset too small");
  if((tmp.fingerprint == FINGERPRINT_FILEBLOCK) &&
     (tmp.block_count != 0) &&
     (tmp.byte_count == 0))
    _throw("invalid OperaFS linked mem file entry: {}",
           "block count without byte count");

  lmfe_ = tmp;
}
