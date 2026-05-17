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

#include "error.hpp"
#include "fmt.hpp"
#include "tdo_romtag.hpp"
#include "tdo_fs_walker.hpp"

#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_set>


namespace fs = std::filesystem;
typedef TDO::FSWalker::Callbacks Callbacks;


static
bool
romtag_size_is_byte_count(const TDO::ROMTag &tag_)
{
  switch(tag_.type)
    {
    case RSA_BLOCKS_ALWAYS:
      return false;
    }

  return true;
}

static
void
update_record(const TDO::ROMTagVec &tags_,
              TDO::DirectoryRecord &dr_)
{
  bool matched = false;
  uint32_t matched_avatar = 0;
  uint32_t matched_byte_count = 0;

  for(const auto &tag : tags_)
    {
      if(!romtag_size_is_byte_count(tag))
        continue;
      // Skip tags whose offset+1 would wrap in u32 space; comparing
      // such a wrapped value against avatar_list[i] could produce a
      // false match on a record whose first avatar is 0 and let a
      // malformed tag overwrite byte_count with arbitrary data.
      if(tag.offset == std::numeric_limits<uint32_t>::max())
        continue;

      // Iterate over avatar_list directly rather than through
      // last_avatar_index. The parser maintains
      // avatar_list.size() == last_avatar_index + 1, but ranging on
      // the container removes the indirect dependency and is robust
      // against any future caller that constructs a DirectoryRecord
      // without going through DevStream::read().
      for(const uint32_t avatar : dr_.avatar_list)
        {
          if(tag.offset+1 == avatar)
            {
              if(matched)
                {
                  // Multiple ROMTags claim the same DirectoryRecord
                  // (each via some avatar in the record's
                  // avatar_list). Keep the first match (preserving
                  // previous behavior) and warn rather than silently
                  // picking last-writer-wins. Report both avatars:
                  // when two tags hit different avatars of the same
                  // record, naming only the first match's block
                  // sends users hunting for a non-existent collision.
                  fmt::print(stderr,
                             "3dt: warning: multiple ROM tags reference directory record "
                             "(kept avatar {} size {}b, ignoring avatar {} size {}b)\n",
                             matched_avatar,
                             matched_byte_count,
                             avatar,
                             tag.size);
                }
              else
                {
                  dr_.byte_count = tag.size;
                  matched = true;
                  matched_avatar = avatar;
                  matched_byte_count = tag.size;
                }
              break;
            }
        }
    }
}

static
Error
decode_v1_filename(const TDO::DirectoryRecord &dr_,
                   std::string                &filename_)
{
  static constexpr std::size_t filename_size = 32;

  const void *terminator;

  terminator = memchr(dr_.filename,'\0',filename_size);
  if(terminator != nullptr)
    {
      const std::size_t length = static_cast<const char*>(terminator) - dr_.filename;

      if(length == 0)
        return "invalid empty directory record filename";

      filename_.assign(dr_.filename,length);
    }
  else
    {
      filename_.assign(dr_.filename,filename_size);
    }

  if(filename_.empty())
    return "invalid empty directory record filename";
  if(filename_ == "." || filename_ == "..")
    return "invalid directory record filename";
  if(filename_.find_first_of("/\\") != std::string::npos)
    return "invalid directory record filename";

  return Error();
}

static
std::string
display_path(const fs::path &path_)
{
  if(path_.empty())
    return "/";
  return path_.generic_string();
}

static
Error
validate_v1_dir_block_link(const char              *name_,
                           const std::int32_t      block_,
                           const std::uint32_t     block_count_,
                           const fs::path         &path_)
{
  const std::string path = display_path(path_);

  if(block_ < -1)
    return {std::string("invalid OperaFS directory header ") + name_ +
            " for " + path};
  if((block_ != -1) && (static_cast<std::uint32_t>(block_) >= block_count_))
    return {std::string("invalid OperaFS directory header ") + name_ +
            " for " + path};

  return Error();
}

static
Error
validate_v1_dir_block_prev(const TDO::DirectoryHeader &header_,
                           const std::int32_t         expected_prev_block_,
                           const std::uint32_t        block_count_,
                           const fs::path            &path_)
{
  Error err;
  const std::string path = display_path(path_);

  err = validate_v1_dir_block_link("prev_block",
                                   header_.prev_block,
                                   block_count_,
                                   path_);
  if(err)
    return err;

  if(header_.prev_block != expected_prev_block_)
    return {"invalid OperaFS directory header prev_block for " + path};

  return Error();
}

static
Error
validate_v1_dir_block_bounds(const std::int64_t block_start_,
                             const std::uint32_t block_size_,
                             const std::int64_t pos_,
                             const char         *what_)
{
  if(block_size_ == 0)
    return {"invalid OperaFS directory block size"};
  if(pos_ < block_start_)
    return {std::string("invalid OperaFS ") + what_ + ": before directory block"};

  const std::int64_t block_end = block_start_ + static_cast<std::int64_t>(block_size_);

  if(pos_ > block_end)
    return {std::string("invalid OperaFS ") + what_ + ": beyond directory block"};

  return Error();
}

class Impl
{
public:
  Impl(std::iostream &ios_,
       Callbacks     &callbacks_,
       bool           use_existing_romtags_)

    : _callbacks(callbacks_),
      _stream(ios_),
      _use_existing_romtags(use_existing_romtags_)
  {

  }

public:
  Error
  walk_v1_dir_block(const TDO::DiscLabel &label_,
                      const TDO::ROMTagVec &romtags_,
                      const std::int64_t    active_dir_byte_pos_,
                      const std::int64_t    active_dir_end_,
                      const std::int64_t    dh_data_byte_pos_,
                      const std::uint32_t   dir_block_size_,
                      const std::uint32_t   block_count_,
                      const std::int32_t    prev_block_,
                      const fs::path       &path_,
                      std::int32_t         &next_block_,
                      bool                 &last_in_dir_)
  {
    TDO::DirectoryHeader dh;
    std::int64_t data_byte_pos;
    Error err;

    next_block_ = -1;
    last_in_dir_ = false;

    if(dir_block_size_ < sizeof(TDO::DirectoryHeader))
      return {"invalid OperaFS directory block: smaller than directory header"};

    data_byte_pos = dh_data_byte_pos_;
    _stream.data_byte_seek(data_byte_pos);
    _stream.read(dh);

    err = validate_v1_dir_block_prev(dh,prev_block_,block_count_,path_);
    if(err)
      return err;
    next_block_ = dh.next_block;

    _callbacks(path_,dh,_stream);

    data_byte_pos += dh.first_entry_offset;
    err = validate_v1_dir_block_bounds(dh_data_byte_pos_,dir_block_size_,data_byte_pos,"directory entry offset");
    if(err)
      return err;

    const std::int64_t first_free_byte_pos =
      dh_data_byte_pos_ + static_cast<std::int64_t>(dh.first_free_byte);

    err = validate_v1_dir_block_bounds(dh_data_byte_pos_,dir_block_size_,first_free_byte_pos,"directory free offset");
    if(err)
      return err;
    if(data_byte_pos == first_free_byte_pos)
      {
        if(next_block_ == -1)
          last_in_dir_ = true;
        return Error();
      }

    _stream.data_byte_seek(data_byte_pos);

    while(true)
      {
        const std::int64_t dr_pos = _stream.data_byte_tell();
        // file_tell() returns s64; the callback contract takes the
        // record's file position as uint32_t. Match the v2 walker's
        // pattern (see walk_v2 below) and reject any v1 directory
        // record whose file offset would not fit in u32 rather than
        // silently narrowing — image_size is bounded only by the
        // underlying stream and can exceed 4 GiB.
        const s64 dr_file_pos_s64 = _stream.file_tell();
        if((dr_file_pos_s64 < 0) ||
           (dr_file_pos_s64 > static_cast<s64>(std::numeric_limits<uint32_t>::max())))
          return {"invalid OperaFS v1 directory record: file position out of range"};
        const std::uint32_t dr_file_pos = static_cast<std::uint32_t>(dr_file_pos_s64);
        TDO::DirectoryRecord dr;
        std::string decoded_filename;
        std::int64_t next_pos;

        if(dr_pos < data_byte_pos)
          return {"invalid OperaFS directory read position: reversed record offset"};
        if(dr_pos >= first_free_byte_pos)
          break;
        err = validate_v1_dir_block_bounds(dh_data_byte_pos_,dir_block_size_,dr_pos,"directory record offset");
        if(err)
          return err;

        _stream.read(dr);

        next_pos = _stream.data_byte_tell();
        if(next_pos <= dr_pos)
          return {"invalid OperaFS directory read position: non-advancing record read"};
        if(next_pos > first_free_byte_pos)
          return {"invalid OperaFS directory record: extends beyond directory data"};
        err = validate_v1_dir_block_bounds(dh_data_byte_pos_,dir_block_size_,next_pos,"directory record end");
        if(err)
          return err;

        update_record(romtags_,dr);
        err = decode_v1_filename(dr,decoded_filename);
        if(err)
          {
            TDO::PosGuard guard(_stream);
            err = _callbacks.invalid_filename(path_,
                                              decoded_filename,
                                              dr,
                                              dr_file_pos,
                                              err,
                                              _stream);
            if(err)
              return err;
          }
        else
          {
            {
              TDO::PosGuard guard(_stream);
              _callbacks(path_ / decoded_filename,dr,dr_file_pos,_stream);
            }

            if(dr.is_directory())
              {
                const std::int64_t child_dir_byte_pos =
                  static_cast<std::int64_t>(dr.avatar_list[0]) * label_.volume_block_size;

                if((child_dir_byte_pos >= active_dir_byte_pos_) && (child_dir_byte_pos < active_dir_end_))
                  return {"invalid OperaFS directory recursion target: non-advancing child directory block"};

                err = walk_v1_dir(label_,romtags_,dr,path_ / decoded_filename);
                if(err)
                  return err;
              }
          }

        if(dr.last_in_dir())
          {
            last_in_dir_ = true;
            break;
          }
        if(dr.last_in_block())
          break;
        if(_stream.data_byte_tell() >= first_free_byte_pos)
          break;

        data_byte_pos = next_pos;
      }

    return Error();
  }

  Error
  walk_v1_dir(const TDO::DiscLabel &label_,
              const TDO::ROMTagVec &romtags_,
              const std::uint32_t   dir_block_,
              const std::uint32_t   dir_block_size_,
              const std::uint32_t   dir_block_count_,
              const fs::path       &path_)
  {
    // u32 * u32 silently wraps in u32 before being widened to s64.
    // Compute in u64 first and reject if the start or end position
    // would not fit in s64 (the underlying stream cursor type),
    // matching the protection on the v2 path. Both inputs are read
    // raw from the on-disc directory record so an attacker-shaped
    // image can drive the multiply past 4 GiB.
    std::int64_t active_dir_byte_pos;
    std::int64_t active_dir_end;
    {
      const std::uint64_t start_u64 =
        static_cast<std::uint64_t>(dir_block_) * label_.volume_block_size;
      const std::uint64_t span_u64 =
        static_cast<std::uint64_t>(dir_block_size_) * dir_block_count_;
      const std::uint64_t s64_max =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
      if(start_u64 > s64_max)
        return {"invalid OperaFS v1 directory: start position overflow"};
      if(span_u64 > (s64_max - start_u64))
        return {"invalid OperaFS v1 directory: end position overflow"};
      active_dir_byte_pos = static_cast<std::int64_t>(start_u64);
      active_dir_end      = static_cast<std::int64_t>(start_u64 + span_u64);
    }
    TDO::PosGuard pos_guard(_stream);
    std::int32_t block;
    std::int32_t prev_block;
    std::unordered_set<std::uint32_t> visited;

    if((dir_block_count_ != 0) && (dir_block_size_ == 0))
      return {"invalid OperaFS directory metadata: zero directory block size"};
    if(dir_block_count_ == 0)
      return Error();

    block = 0;
    prev_block = -1;
    while(block != -1)
      {
        Error err;
        std::int32_t next_block;
        std::int64_t dh_data_byte_pos;
        bool last_in_dir;

        if(static_cast<std::uint32_t>(block) >= dir_block_count_)
          return {"invalid OperaFS directory header next_block for " + display_path(path_)};
        if(!visited.insert(static_cast<std::uint32_t>(block)).second)
          return {"invalid OperaFS directory header next_block loop for " + display_path(path_)};

        dh_data_byte_pos =
          active_dir_byte_pos + (static_cast<std::int64_t>(dir_block_size_) * block);

        err = walk_v1_dir_block(label_,
                                romtags_,
                                active_dir_byte_pos,
                                active_dir_end,
                                dh_data_byte_pos,
                                dir_block_size_,
                                dir_block_count_,
                                prev_block,
                                path_,
                                next_block,
                                last_in_dir);
        if(err)
          return err;
        if(last_in_dir)
          break;

        err = validate_v1_dir_block_link("next_block",
                                         next_block,
                                         dir_block_count_,
                                         path_);
        if(err)
          return err;
        if(next_block == -1)
          break;

        prev_block = block;
        block = next_block;
      }

    return Error();
  }

  Error
  walk_v1_dir(const TDO::DiscLabel       &label_,
              const TDO::ROMTagVec       &romtags_,
              const TDO::DirectoryRecord &parent_,
              const fs::path             &path_)
  {
    return walk_v1_dir(label_,
                       romtags_,
                       parent_.avatar_list[0],
                       parent_.block_size,
                       parent_.block_count,
                       path_);
  }

  Error
  walk_v1_root_dir(const TDO::DiscLabel &label_,
                   const TDO::ROMTagVec &romtags_,
                   const fs::path       &path_)
  {
    return walk_v1_dir(label_,
                       romtags_,
                       label_.root_directory_avatar_list[0],
                       label_.root_directory_block_size,
                       label_.root_directory_block_count,
                       path_);
  }

  Error
  walk_v2(const TDO::DiscLabel &label_,
          const fs::path       &path_)
  {
    s64 pos;
    s64 image_size;
    std::unordered_set<s64> visited;

    if(label_.root_directory_block_size == 0)
      return {"invalid OperaFS v2 root directory: zero block size"};

    image_size = _stream.size_in_bytes();
    // Compute the root-directory file position in u64 to avoid signed
    // overflow UB. Both fields are u32 read raw from the disc label, so
    // their product fits in u64 (max (2^32-1)^2 < 2^64) but can exceed
    // INT64_MAX (max ~9.22e18 < (2^32-1)^2 ~1.84e19). Range-check before
    // narrowing to s64.
    {
      const u64 pos_u64 = (static_cast<u64>(label_.root_directory_avatar_list[0]) *
                           static_cast<u64>(label_.root_directory_block_size));
      if(pos_u64 > static_cast<u64>(std::numeric_limits<s64>::max()))
        return {"invalid OperaFS v2 root directory: position overflow"};
      pos = static_cast<s64>(pos_u64);
    }
    if((pos < 0) || (pos >= image_size))
      return {"invalid OperaFS v2 root directory: position out of bounds"};

    while(true)
      {
        TDO::DirectoryRecord dr;
        TDO::LinkedMemFileEntry lmfe;
        s64 next_pos;

        if(!visited.insert(pos).second)
          return {"invalid OperaFS v2 directory link: revisited offset"};

        _stream.data_byte_seek(pos);
        _stream.read(lmfe);

        if(lmfe.fingerprint == FINGERPRINT_FILEBLOCK)
          {
            // The Callbacks operator() takes the file position as
            // const uint32_t. pos is now s64 and bounded by
            // image_size, which can exceed 4 GiB; refuse rather than
            // silently narrowing.
            if((pos < 0) ||
               (pos > static_cast<s64>(std::numeric_limits<uint32_t>::max())))
              return {"invalid OperaFS v2 directory entry: position out of range"};

            std::string decoded_filename;
            const void *terminator =
              memchr(lmfe.filename,'\0',sizeof(lmfe.filename));
            if(terminator != nullptr)
              decoded_filename.assign(lmfe.filename,
                                      static_cast<const char*>(terminator) -
                                      lmfe.filename);
            else
              decoded_filename.assign(lmfe.filename,sizeof(lmfe.filename));

            dr.flags = 0;
            dr.unique_identifier = lmfe.unique_identifier;
            dr.type = lmfe.type;
            dr.block_size = label_.volume_block_size;
            dr.byte_count = lmfe.byte_count;
            dr.block_count = lmfe.block_count;
            dr.burst = 0;
            dr.gap = 0;
            memcpy(dr.filename,lmfe.filename,sizeof(dr.filename));
            dr.last_avatar_index = 0;
            // avatar_list elements are uint32_t; pos is now s64 and
            // bounded only by image_size, which can exceed 4 GiB.
            // Reject any v2 entry whose first-avatar offset would not
            // fit in uint32_t rather than silently narrowing.
            {
              const s64 first_avatar = pos + static_cast<s64>(lmfe.header_block_count);
              if((first_avatar < 0) ||
                 (first_avatar > static_cast<s64>(std::numeric_limits<uint32_t>::max())))
                return {"invalid OperaFS v2 file entry: avatar position out of range"};
              dr.avatar_list.push_back(static_cast<uint32_t>(first_avatar));
            }

            TDO::PosGuard guard(_stream);
            _callbacks(path_ / decoded_filename,dr,static_cast<uint32_t>(pos),_stream);
          }

        next_pos = static_cast<s64>(lmfe.flink_offset);
        if(next_pos <= pos)
          break;
        if(next_pos >= image_size)
          return {"invalid OperaFS v2 directory link: flink_offset out of bounds"};
        pos = next_pos;
      }

    return Error();
  }

  Error
  walk()
  {
    Error err;
    fs::path path;
    TDO::DiscLabel dl;
    TDO::ROMTagVec romtags;

    _stream.setup();

    dl = _stream.disc_label();

    if(_use_existing_romtags)
      romtags = _stream.romtags();

    _callbacks.begin();
    switch(dl.volume_structure_version)
      {
      case VOLUME_STRUCTURE_OPERA_READONLY:
        err = walk_v1_root_dir(dl,romtags,path);
        break;
      case VOLUME_STRUCTURE_LINKED_MEM:
        err = walk_v2(dl,path);
        break;
      default:
        break;
      }
    _callbacks.end();

    return err;
  }

private:
  Callbacks      &_callbacks;
  TDO::DevStream  _stream;
  bool            _use_existing_romtags;
};

namespace TDO
{
  FSWalker::FSWalker(std::iostream &ios_,
                     Callbacks     &callbacks_,
                     bool           use_existing_romtags_)
    : _callbacks(callbacks_),
      _ios(ios_),
      _use_existing_romtags(use_existing_romtags_)
  {
  }

  FSWalker::FSWalker(DevStream &stream_,
                     Callbacks &callbacks_,
                     bool       use_existing_romtags_)
    : _callbacks(callbacks_),
      _ios(stream_.iostream()),
      _use_existing_romtags(use_existing_romtags_)
  {
  }

  void
  FSWalker::walk()
  {
    Impl impl(_ios,_callbacks,_use_existing_romtags);

    Error err = impl.walk();
    if(err)
      throw err;
  }
}
