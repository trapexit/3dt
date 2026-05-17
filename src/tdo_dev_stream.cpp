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

#include "tdo_dev_stream.hpp"

#include "tdo_disc_label.hpp"
#include "tdo_linked_mem_file_entry.hpp"
#include "tdo_safe_narrow.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <ios>
#include <limits>
#include <stdexcept>

static constexpr int TDO_SECTOR_SIZE = 2048;
static constexpr int CDROM_SECTOR_SIZE = 2352;
static constexpr int SYNC_PATTERN_SIZE = 12;
static constexpr u64 MAX_RECOGNITION_SCAN_BYTES = 1024 * 1024;
static constexpr u32 MAX_DIRECTORY_AVATAR_INDEX = ROOT_HIGHEST_AVATAR;
static constexpr u32 DIRECTORY_HEADER_SIZE = sizeof(TDO::DirectoryHeader);
static constexpr u32 M1_MAX_ROMTAG_BLOCK_SIZE = 2048;
static constexpr u32 M1_RSA_SIGNATURE_SIZE = 64;
static constexpr u32 M2_MAX_ROMTAG_BLOCK_SIZE = 8192;
typedef std::array<u8,CDROM_SECTOR_SIZE> CDROMSectorBuf;
typedef std::array<u8,SYNC_PATTERN_SIZE> CDROMSyncPatternBuf;
typedef std::array<u8,VOLUME_SYNC_BYTE_LEN> VolumeSyncByteBuf;
static constexpr CDROMSyncPatternBuf MODE1_SYNC_PATTERN = {0x00,0xFF,0xFF,0xFF,
                                                           0xFF,0xFF,0xFF,0xFF,
                                                           0xFF,0xFF,0xFF,0x00};
static constexpr VolumeSyncByteBuf VOLUME_SYNC_BYTES = {0x5A,0x5A,0x5A,0x5A,0x5A};

static
inline
s64
_round_up(s64 number_,
          s64 multiple_)
{
  return (((number_ + multiple_ - 1) / multiple_) * multiple_);
}

static
inline
u64
_div_round_up(u64 number_,
              u64 multiple_)
{
  return ((number_ + multiple_ - 1) / multiple_);
}

static
inline
u64
_checked_s64_add(u64        lhs_,
                 u64        rhs_,
                 const char *msg_)
{
  static constexpr u64 S64_MAX_AS_U64 = static_cast<u64>(S64_MAX);

  if((lhs_ > S64_MAX_AS_U64) ||
     (rhs_ > S64_MAX_AS_U64) ||
     (lhs_ > (S64_MAX_AS_U64 - rhs_)))
    throw Error(msg_);

  return (lhs_ + rhs_);
}

static
inline
u64
_checked_s64_mul(u64        lhs_,
                 u64        rhs_,
                 const char *msg_)
{
  static constexpr u64 S64_MAX_AS_U64 = static_cast<u64>(S64_MAX);

  if((rhs_ != 0) && (lhs_ > (S64_MAX_AS_U64 / rhs_)))
    throw Error(msg_);

  return (lhs_ * rhs_);
}

static
inline
s64
_data_byte_to_file_offset(s64 data_byte_,
                          u64 data_start_offset_,
                          u64 device_block_header_,
                          u64 device_block_data_size_,
                          u64 device_block_size_)
{
  static constexpr const char *MSG = "data offset is too large";
  u64 block;
  u64 extra;
  u64 file_offset;
  u64 pos;

  if(data_byte_ < 0)
    throw Error("data offset must be non-negative");
  if(device_block_data_size_ == 0)
    throw Error("device block data size must be non-zero");

  pos   = _checked_s64_add(data_byte_,data_start_offset_,MSG);
  block = (pos / device_block_data_size_);
  extra = (pos % device_block_data_size_);

  file_offset  = _checked_s64_mul(block,device_block_size_,MSG);
  file_offset  = _checked_s64_add(file_offset,device_block_header_,MSG);
  file_offset  = _checked_s64_add(file_offset,extra,MSG);

  return static_cast<s64>(file_offset);
}

static
inline
bool
_is_m2_volume(const TDO::DiscLabel &dl_)
{
  return ((dl_.volume_flags & VOLUME_FLAG_M2) != 0);
}

static
inline
u64
_disc_label_size_in_bytes(const TDO::DiscLabel &dl_)
{
  if(_is_m2_volume(dl_))
    return sizeof(TDO::ExtDiscLabel);

  return sizeof(TDO::DiscLabel);
}

static
inline
void
_validate_m2_romtag_count(u32 count_)
{
  if(count_ > (M2_MAX_ROMTAG_BLOCK_SIZE / sizeof(TDO::ROMTag)))
    throw Error(fmt::format("invalid M2 ROMTag count: {}",count_));
}

static
inline
void
_swap(u32 &u32_)
{
  u32_ = __builtin_bswap32(u32_);
}

static
inline
void
_swap(s32 &s32_)
{
  s32_ = __builtin_bswap32(s32_);
}

TDO::DevStream::DevStream(std::iostream &ios_)
  : _device_block_header(0),
    _device_block_data_size(0),
    _device_block_footer(0),
    _data_start_offset(0),
    _disc_label_size_in_bytes(0),
    _disc_label_block(0),
    _romtags_block(0),
    _romtags_entry_count(0),
    _romtags_entry_count_is_explicit(false),
    _ios(ios_)
{
}

void
TDO::DevStream::find_label()
{
  static constexpr std::array<char, 6> PATTERN =
    {RECORD_STD_VOLUME, VOLUME_SYNC_BYTE, VOLUME_SYNC_BYTE,
     VOLUME_SYNC_BYTE, VOLUME_SYNC_BYTE, VOLUME_SYNC_BYTE};
  static constexpr u64 CHUNK_SIZE = 65536;
  static constexpr u64 OVERLAP = PATTERN.size() - 1;
  std::vector<char> buf(CHUNK_SIZE + OVERLAP);
  u64 bytes_scanned = 0;

  _ios.seekg(0);
  while(_ios && !_ios.eof() && (bytes_scanned < MAX_RECOGNITION_SCAN_BYTES))
    {
      const u64 seek_pos = (bytes_scanned > OVERLAP) ? bytes_scanned - OVERLAP : 0;
      _ios.seekg(seek_pos);

      const u64 remaining = MAX_RECOGNITION_SCAN_BYTES - bytes_scanned;
      const u64 to_read = std::min<u64>(CHUNK_SIZE + OVERLAP, remaining + OVERLAP);
      _ios.read(buf.data(), to_read);
      const std::streamsize n = _ios.gcount();
      if(n <= 0)
        break;

      const u64 scan_limit = n - OVERLAP;
      for(u64 i = 0; i < scan_limit; i++)
        {
          if(std::memcmp(&buf[i], PATTERN.data(), PATTERN.size()) == 0)
            {
              _ios.clear(_ios.rdstate() & std::ios::badbit);
              _ios.seekg(seek_pos + i);
              return;
            }
        }

      bytes_scanned += scan_limit;
    }

  throw Error("no OperaFS label was found");
}

bool
TDO::DevStream::is_mode1_2352()
{
  CDROMSectorBuf buf;

  _ios.seekg(0);
  _ios.read((char*)&buf[0],buf.size());

  const bool has_mode1_sync_pattern =
    (memcmp(&buf[0],&MODE1_SYNC_PATTERN[0],MODE1_SYNC_PATTERN.size()) == 0);
  const bool has_mode1_sector_marker = (buf[0x0F] == 0x01);

  return (has_mode1_sync_pattern && has_mode1_sector_marker);
}

void
TDO::DevStream::setup()
{
  TDO::DiscLabel dl;

  if(is_mode1_2352())
    {
      _device_block_header = 16;
      _device_block_footer = 288;
    }
  else
    {
      _device_block_header = 0;
      _device_block_footer = 0;
    }

  find_label();

  {
    TDO::PosGuard guard(*this);

    read(dl);
    _device_block_data_size = dl.volume_block_size;
    _disc_label_size_in_bytes = ::_disc_label_size_in_bytes(dl);
    _romtags_entry_count = 0;
    _romtags_entry_count_is_explicit = false;
    if(::_is_m2_volume(dl))
      {
        read(_romtags_entry_count);
        ::_validate_m2_romtag_count(_romtags_entry_count);
        _romtags_entry_count_is_explicit = true;
      }
  }

  _data_start_offset = data_byte_tell();
  _disc_label_block = data_block_tell();
  _romtags_block = (_disc_label_block +
                    ::_div_round_up(_disc_label_size_in_bytes,
                                     _device_block_data_size));

  _ios.seekg(0);
}

static
bool
is_romfs(TDO::DevStream &stream_)
{
  return ((stream_.device_block_header()    == 0) &&
          (stream_.device_block_data_size() == 4) &&
          (stream_.device_block_footer()    == 0));
}

bool
TDO::DevStream::has_romtags()
{
  TDO::PosGuard pos_guard(*this);

  if(::is_romfs(*this))
    return false;
  if(_romtags_entry_count_is_explicit)
    return (_romtags_entry_count != 0);

  return true;
}

TDO::DiscLabel
TDO::DevStream::disc_label()
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
  return _disc_label_size_in_bytes;
}

u64
TDO::DevStream::disc_label_block() const
{
  return _disc_label_block;
}

u64
TDO::DevStream::_romtags_count_impl()
{
  TDO::PosGuard guard(*this);

  if(!has_romtags())
    return 0;

  data_block_seek(romtags_block());
  if(_romtags_entry_count_is_explicit)
    return _romtags_entry_count;

  u64 count = 0;
  u64 rttlen = 0;
  while(true)
    {
      TDO::ROMTag romtag;

      read(romtag);
      count++;
      if(romtag.sub_systype == 0)
        break;

      rttlen += sizeof(TDO::ROMTag);
      if(rttlen > (M1_MAX_ROMTAG_BLOCK_SIZE -
                   M1_RSA_SIGNATURE_SIZE -
                   sizeof(TDO::ROMTag)))
        throw Error("invalid OperaFS ROMTag table: missing terminator");
    }

  return count;
}

TDO::ROMTagVec
TDO::DevStream::romtags()
{
  TDO::ROMTagVec romtags;
  TDO::PosGuard guard(*this);

  if(!has_romtags())
    return romtags;

  data_block_seek(romtags_block());
  if(_romtags_entry_count_is_explicit)
    {
      for(u32 i = 0; i < _romtags_entry_count; i++)
        {
          TDO::ROMTag romtag;

          read(romtag);
          romtags.emplace_back(romtag);
        }
    }
  else
    {
      u64 rttlen;

      rttlen = 0;
      while(true)
        {
          TDO::ROMTag romtag;

          read(romtag);
          if(romtag.sub_systype == 0)
            break;

          rttlen += sizeof(TDO::ROMTag);
          if(rttlen > (M1_MAX_ROMTAG_BLOCK_SIZE -
                       M1_RSA_SIGNATURE_SIZE -
                       sizeof(TDO::ROMTag)))
            throw Error("invalid OperaFS ROMTag table: missing terminator");

          romtags.emplace_back(romtag);
        }
    }

  return romtags;
}

std::optional<TDO::ROMTag>
TDO::DevStream::romtag(const int type_)
{
  TDO::PosGuard guard(*this);

  if(!has_romtags())
    return {};

  for(const auto &romtag : romtags())
    if(romtag.type == type_)
      return romtag;

  return {};
}

u64
TDO::DevStream::romtags_block() const
{
  return _romtags_block;
}

u64
TDO::DevStream::romtags_count()
{
  return _romtags_count_impl();
}

u64
TDO::DevStream::romtags_size_in_bytes()
{
  return (romtags_count() * sizeof(TDO::ROMTag));
}

u64
TDO::DevStream::data_offset() const
{
  return _data_start_offset;
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
TDO::DevStream::device_block_count()
{
  TDO::PosGuard guard(*this);

  // Check failbit on the seek and use file_tell() (which throws on
  // tellg == -1) rather than letting a failed seek leave tellg at -1.
  // streamoff(-1) / u64(blocksize) undergoes the usual arithmetic
  // conversion (signed -> unsigned) producing 0xFFFF... / blocksize,
  // a huge u64 that silently bypasses downstream end-bound checks
  // such as safe_romtag_first_data_block's offset+1 >= file_blocks
  // guard.
  _ios.seekg(0,_ios.end);
  if(!_ios.good())
    throw Error("stream seek to end failed in device_block_count");

  const s64 pos = file_tell();
  return (static_cast<u64>(pos) / device_block_size());
}

s64
TDO::DevStream::data_block_to_file_offset(s64 data_block_) const
{
  s64 data_byte;

  if(data_block_ < 0)
    throw Error("data block must be non-negative");

  data_byte = _checked_s64_mul(data_block_,
                               _device_block_data_size,
                               "data block offset is too large");

  return _data_byte_to_file_offset(data_byte,
                                   _data_start_offset,
                                   _device_block_header,
                                   _device_block_data_size,
                                   device_block_size());
}

s64
TDO::DevStream::file_tell() const
{
  s64 rv;

  rv = _ios.tellg();
  if(rv == -1)
    throw Error("stream position error");

  return rv;
}

s64
TDO::DevStream::_file_pos_to_data_byte_pos(const s64 pos_) const
{
  static constexpr const char *MSG = "file position is out of range";
  u64 pos;
  u64 block;
  u64 extra;
  u64 result;

  // Mirror the structure of _data_byte_to_file_offset above: enforce
  // pos_ >= 0 explicitly and do all arithmetic in u64. The previous
  // implementation mixed an s64 destination variable with u64 member
  // operands, which produced two wrap hazards:
  //  - (extra - _device_block_header) wraps to ~U64_MAX when pos_
  //    lands inside the 16-byte block header of a mode-1/2352 image.
  //  - (pos -= _data_start_offset) wraps when pos_ falls before the
  //    start of the data region.
  // Both used to silently produce a huge positive s64 result.
  if(pos_ < 0)
    throw Error("file position must be non-negative");
  if(device_block_size() == 0)
    throw Error("device block size must be non-zero");

  pos   = static_cast<u64>(pos_);
  block = (pos / device_block_size());
  extra = (pos % device_block_size());

  if(extra < _device_block_header)
    throw Error("file position points inside device block header");

  result  = _checked_s64_mul(block,_device_block_data_size,MSG);
  result  = _checked_s64_add(result,extra - _device_block_header,MSG);
  if(result < _data_start_offset)
    throw Error("file position is before start of data region");
  result -= _data_start_offset;

  return TDO::checked_narrow_u64_to_s64(result,"data byte position");
}

s64
TDO::DevStream::data_byte_tell(const s64 pos_) const
{
  return _file_pos_to_data_byte_pos(pos_);
}

s64
TDO::DevStream::data_byte_tell() const
{
  return data_byte_tell(file_tell());
}

s64
TDO::DevStream::data_block_tell(const s64 pos_) const
{
  return (_file_pos_to_data_byte_pos(pos_) / _device_block_data_size);
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
  // Match the protective shape of _file_pos_to_data_byte_pos: reject
  // a negative pos_ rather than letting s64/u64 mixed division
  // reinterpret it as a huge u64 quotient that the caller silently
  // accepts. The block-count quotient itself is bounded by pos_ so a
  // direct cast back to s64 is safe once non-negative.
  if(pos_ < 0)
    throw Error("file position must be non-negative");
  if(device_block_size() == 0)
    throw Error("device block size must be non-zero");

  return static_cast<s64>(static_cast<u64>(pos_) / device_block_size());
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
  _ios.seekp(pos_);
}

void
TDO::DevStream::data_byte_seek(const s64 pos_)
{
  s64 pos;

  pos = _data_byte_to_file_offset(pos_,
                                  _data_start_offset,
                                  _device_block_header,
                                  _device_block_data_size,
                                  device_block_size());

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
  static constexpr const char *MSG = "device block offset is too large";
  u64 pos;

  // Mirror _data_byte_to_file_offset / fix to _file_pos_to_data_byte_pos:
  // reject negative pos_ explicitly so the s64*u64 multiplication does
  // not silently reinterpret a sign-bit value as a huge u64. Route the
  // multiply through _checked_s64_mul so an overlarge image-block
  // index produces an Error rather than a wrapped file offset, and
  // narrow the u64 back through checked_narrow_u64_to_s64 for symmetry.
  if(pos_ < 0)
    throw Error("device block must be non-negative");

  pos = _checked_s64_mul(static_cast<u64>(pos_),device_block_size(),MSG);

  file_seek(TDO::checked_narrow_u64_to_s64(pos,"device block file offset"));
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
  if(!_ios.good())
    _throw("bad stream state before read");

  _ios.read(buf_,size_);

  if(!_ios.good())
    _throw("bad stream state after read");
}

void
TDO::DevStream::write(const char *buf_,
                      const u64   size_)
{
  _ios.write(buf_,size_);
  if(!_ios.good())
    _throw("bad stream state after write");
}

void
TDO::DevStream::read_data_blocks(std::vector<char> &v_,
                                 const s64          pos_,
                                 const s64          blocks_)
{
  size_t end;

  end = v_.size();
  v_.resize(end + (device_block_data_size() * blocks_));
  for(s64 i = 0; i < blocks_; i++)
    {
      data_block_seek(pos_ + i);
      read(&v_[end],device_block_data_size());
      end += device_block_data_size();
    }
}

void
TDO::DevStream::read_data_bytes_from_block(char     *buf_,
                                             const s64 block_pos_,
                                             const s64 bytes_)
{
  read_data_bytes(buf_,
                  (block_pos_ * device_block_data_size()),
                  bytes_);
}

void
TDO::DevStream::read_data_bytes(char     *buf_,
                               const s64 pos_,
                               const s64 bytes_)
{
  s64 pos;
  s64 bytes_read;
  s64 bytes_to_read;
  s64 block_size;

  bytes_read = 0;
  block_size = device_block_data_size();

  pos = pos_;
  for(s64 bytes_left = bytes_; bytes_left > 0;)
    {
      data_byte_seek(pos);

      bytes_to_read = (block_size - (pos % block_size));
      bytes_to_read = std::min(bytes_to_read,bytes_left);

      read(&buf_[bytes_read],bytes_to_read);

      pos        += bytes_to_read;
      bytes_left -= bytes_to_read;
      bytes_read += bytes_to_read;
    }
}

void
TDO::DevStream::read_data_bytes_from_block(std::vector<char> &v_,
                                           const s64          block_pos_,
                                           const s64          bytes_)
{
  s64 vec_end = v_.size();
  v_.resize(v_.size() + bytes_);
  read_data_bytes(&v_[vec_end],
                  (block_pos_ * device_block_data_size()),
                  bytes_);
}

void
TDO::DevStream::read_data_bytes(std::vector<char> &v_,
                                const s64          pos_,
                                const s64          bytes_)
{
  s64 vec_end = v_.size();
  v_.resize(v_.size() + bytes_);
  read_data_bytes(&v_[vec_end],pos_,bytes_);
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
  _swap(u32_);
}

void
TDO::DevStream::write(u32 u32_)
{
  _swap(u32_);
  write((const char*)&u32_,sizeof(u32));
}

void
TDO::DevStream::read(s32 &s32_)
{
  read((char*)&s32_,sizeof(s32));
  _swap(s32_);
}

void
TDO::DevStream::write(s32 s32_)
{
  _swap(s32_);
  write((const char*)&s32_,sizeof(s32));
}

void
TDO::DevStream::read(TDO::DiscLabel &dl_)
{
  TDO::DiscLabel tmp;

  read(tmp.record_type);
  read(tmp.volume_sync_bytes);
  read(tmp.volume_structure_version);
  read(tmp.volume_flags);
  read(tmp.volume_commentary);
  read(tmp.volume_identifier);
  read(tmp.volume_unique_identifier);
  read(tmp.volume_block_size);
  read(tmp.volume_block_count);
  read(tmp.root_unique_identifier);
  read(tmp.root_directory_block_count);
  read(tmp.root_directory_block_size);
  read(tmp.root_directory_last_avatar_index);
  read(tmp.root_directory_avatar_list);

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

  // Bound volume_block_count against the raw file size. Consumers
  // (signer, verifier) multiply this by BLOCK_SIZE / LOG_BLOCK_SIZE
  // to derive digest counts and signature-file sizes, so an
  // unbounded value produces astronomical work and out-of-image
  // reads. We compare against raw size_in_bytes() rather than the
  // device block count because geometry may not yet be initialized
  // when this runs during setup(); the resulting bound is loose
  // but correct (data bytes are always <= raw bytes).
  //
  // size_in_bytes() returns s64 and may be -1 when tellg() fails
  // (or when the stream geometry is not yet known); casting -1 to
  // u64 yields 0xFFFF... and silently disables the bound. Inspect
  // the signed value first and skip the check only when size is
  // genuinely unavailable.
  {
    const u64 vbc = tmp.volume_block_count;
    const u64 vbs = tmp.volume_block_size;
    if((vbs > 0) && (vbc > (std::numeric_limits<u64>::max() / vbs)))
      _throw("invalid OperaFS disc label: volume_block_count overflows volume_block_size math");
    const s64 raw_bytes_signed = size_in_bytes();
    if((raw_bytes_signed > 0) && (vbs > 0))
      {
        const u64 raw_bytes = static_cast<u64>(raw_bytes_signed);
        if((vbc * vbs) > raw_bytes)
          _throw("invalid OperaFS disc label: volume_block_count exceeds image size");
      }
  }

  dl_ = tmp;
}

void
TDO::DevStream::write(const TDO::DiscLabel &dl_)
{
  write(dl_.record_type);
  write(dl_.volume_sync_bytes);
  write(dl_.volume_structure_version);
  write(dl_.volume_flags);
  write(dl_.volume_commentary);
  write(dl_.volume_identifier);
  write(dl_.volume_unique_identifier);
  write(dl_.volume_block_size);
  write(dl_.volume_block_count);
  write(dl_.root_unique_identifier);
  write(dl_.root_directory_block_count);
  write(dl_.root_directory_block_size);
  write(dl_.root_directory_last_avatar_index);
  write(dl_.root_directory_avatar_list);
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

  const u64 dev_block_count = device_block_count();

  if(tmp.next_block < -1)
    _throw("unsafe OperaFS directory metadata: invalid next_block");
  if(tmp.prev_block < -1)
    _throw("unsafe OperaFS directory metadata: invalid prev_block");
  if((tmp.next_block != -1) &&
     (static_cast<u64>(tmp.next_block) >= dev_block_count))
    _throw("unsafe OperaFS directory metadata: next_block exceeds device bounds");
  if((tmp.prev_block != -1) &&
     (static_cast<u64>(tmp.prev_block) >= dev_block_count))
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
  std::array<u32,MAX_DIRECTORY_AVATAR_INDEX + 1> avatar_list;

  read(tmp.flags);
  read(tmp.unique_identifier);
  read(tmp.type);
  read(tmp.block_size);
  read(tmp.byte_count);
  read(tmp.block_count);
  read(tmp.burst);
  read(tmp.gap);
  read(tmp.filename,32);
  read(tmp.last_avatar_index);

  if(tmp.last_avatar_index > MAX_DIRECTORY_AVATAR_INDEX)
    _throw("impossible OperaFS directory record last_avatar_index: {} > {}",
           tmp.last_avatar_index,MAX_DIRECTORY_AVATAR_INDEX);

  for(u32 i = 0; i <= tmp.last_avatar_index; i++)
    read(avatar_list[i]);

  tmp.avatar_list.assign(avatar_list.begin(),
                         avatar_list.begin() + (tmp.last_avatar_index + 1));

  const u32 avatar_count = tmp.last_avatar_index + 1;
  const u32 data_block_size = device_block_data_size();

  if(tmp.avatar_list.size() != avatar_count)
    _throw("impossible OperaFS directory record avatar count: avatar_list.size()={} last_avatar_index+1={}",
           tmp.avatar_list.size(),avatar_count);
  if(tmp.block_size == 0)
    _throw("unsafe OperaFS directory record metadata: zero block_size");
  if((data_block_size == 0) || ((tmp.block_size % data_block_size) != 0))
    _throw("unsafe OperaFS directory record metadata: invalid block_size alignment");

  const u64 dev_block_count = device_block_count();
  const u64 record_dev_block_count =
    (static_cast<u64>(tmp.block_count) *
     (static_cast<u64>(tmp.block_size) / data_block_size));
  const u64 max_byte_count =
    static_cast<u64>(tmp.block_size) * static_cast<u64>(tmp.block_count);

  if((tmp.block_count == 0) && (avatar_count > 1))
    _throw("unsafe OperaFS directory record metadata: avatars without blocks");
  if((tmp.byte_count != 0) && (tmp.block_count == 0))
    _throw("unsafe OperaFS directory record metadata: byte_count without blocks");
  if((tmp.block_count != 0) && (tmp.byte_count > max_byte_count))
    _throw("unsafe OperaFS directory record metadata: byte_count exceeds block capacity");

  if(tmp.block_count != 0)
    {
      if(record_dev_block_count > dev_block_count)
        _throw("unsafe OperaFS directory record metadata: block_count exceeds device bounds");

      for(u32 avatar : tmp.avatar_list)
        {
          if(avatar >= dev_block_count)
            _throw("unsafe OperaFS directory record metadata: avatar exceeds device bounds");
          if(static_cast<u64>(avatar) >
             (dev_block_count - record_dev_block_count))
            _throw("unsafe OperaFS directory record metadata: avatar extent exceeds device bounds");
        }
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
  TDO::LinkedMemFileEntry tmp;

  read(tmp.fingerprint);
  read(tmp.flink_offset);
  read(tmp.blink_offset);
  read(tmp.block_count);
  read(tmp.header_block_count);
  read(tmp.byte_count);
  read(tmp.unique_identifier);
  read(tmp.type);
  read(tmp.filename,sizeof(tmp.filename));

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

s64
TDO::DevStream::size_in_bytes()
{
  s64 size;
  TDO::PosGuard guard(this);

  _ios.seekg(0,_ios.end);
  size = _ios.tellg();

  return size;
}

s64
TDO::DevStream::size_in_device_blocks()
{
  const s64 bytes = size_in_bytes();
  // size_in_bytes() returns -1 when tellg() fails. Letting the s64
  // value mix with the u64 device_block_size() in division promotes
  // -1 to 0xFFFF... via usual arithmetic conversion and yields a
  // huge positive count (~9e15) that silently feeds downstream
  // u32-narrowed sinks (e.g. signer dl.volume_block_count). Propagate
  // the failure sentinel instead.
  if(bytes < 0)
    return bytes;
  return (bytes / static_cast<s64>(device_block_size()));
}

void
TDO::DevStream::resize_multiple(s64 multiple_)
{
  s64 size;
  s64 rounded_size;
  TDO::PosGuard guard(this);

  _ios.seekg(0,_ios.end);
  size = _ios.tellg();

  rounded_size = _round_up(size,multiple_);
  if(rounded_size == size)
    return;

  _ios.seekp(rounded_size - 1);
  _ios.put(0);
  if(!_ios)
    throw Error("failed to resize stream");
}
