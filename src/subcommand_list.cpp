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

#include "log.hpp"
#include "options.hpp"
#include "tdo_fs_walker.hpp"
#include "tdo_safe_narrow.hpp"

#include "fmt.hpp"

#include <fstream>

namespace fs = std::filesystem;
typedef std::function<void(const std::string&,const TDO::DirectoryRecord&,const uint32_t,TDO::DevStream&)> Printer;


static
bool
starts_with(const fs::path &base_,
            const fs::path &full_)
{
  auto biter     = base_.begin();
  auto biter_end = base_.end();
  auto fiter     = full_.begin();
  auto fiter_end = full_.end();

  while((biter != biter_end) && (fiter != fiter_end))
    {
      if(*biter != *fiter)
        return false;

      ++biter;
      ++fiter;
    }

  return (biter == biter_end);
}

static
std::string
display_path(const fs::path    &parent_,
             const std::string &filename_)
{
  std::string path;

  path = parent_.generic_string();
  if(!path.empty() && !filename_.empty() && (filename_[0] != '/'))
    path += "/";
  if(filename_.empty())
    path += "<invalid empty filename>";
  else
    path += filename_;

  return path;
}

static
void
default_header()
{
  fmt::print("Flags      Size         ID Type Filename\n");
}

static
void
default_printer(const std::string          &filepath_,
                const TDO::DirectoryRecord &record_,
                const uint32_t              record_pos_,
                TDO::DevStream             &stream_)
{
  char dir_char;
  char readonly_char;
  char for_fs_char;

  dir_char      = (record_.is_directory() ? 'd' : '-');
  readonly_char = (record_.is_readonly() ? 'r' : '-');
  for_fs_char   = (record_.is_for_fs() ? 'f' : '-');
  fmt::print("{}{}{} {:11L} {:#010x} {:4s} {}\n",
             dir_char,
             readonly_char,
             for_fs_char,
             record_.byte_count,
             record_.unique_identifier,
             record_.type_str(),
             filepath_);
}

static
void
offset_header()
{
  fmt::print("Flags      Size         ID Type  RecOffset     Avatar Filename\n");
}

static
void
file_offset_printer(const std::string          &filepath_,
                    const TDO::DirectoryRecord &record_,
                    const uint32_t              record_pos_,
                    TDO::DevStream             &stream_)
{
  char dir_char;
  char readonly_char;
  char for_fs_char;
  std::uint32_t avatar;

  dir_char      = (record_.is_directory() ? 'd' : '-');
  readonly_char = (record_.is_readonly() ? 'r' : '-');
  for_fs_char   = (record_.is_for_fs() ? 'f' : '-');
  if(record_.avatar_list.empty())
    {
      avatar = 0u;
    }
  else
    {
      // OperaFS targets a 32-bit platform; the avatar's file offset
      // must fit in u32. Compute in s64 so we can detect overflow.
      // The list subcommand surfaces what's in the image, including
      // malformed entries: catch the helper's throw so one bad row
      // does not abort the whole listing (Subcommand::list has no
      // try/catch around walker.walk()), warn to stderr, and display
      // 0 in place of the unrepresentable offset.
      const s64 file_offset =
        stream_.data_block_to_file_offset(record_.avatar_list[0]);
      try
        {
          avatar = TDO::checked_narrow_s64_to_u32(file_offset,
                                                  "avatar file offset for " + filepath_);
        }
      catch(const Error &e)
        {
          fmt::print(stderr,"3dt: warning: {}, displaying 0\n",e.what());
          avatar = 0u;
        }
    }
  fmt::print("{}{}{} {:11L} {:#010x} {:4s} {:#010x} {:#010x} {}\n",
             dir_char,
             readonly_char,
             for_fs_char,
             record_.byte_count,
             record_.unique_identifier,
             record_.type_str(),
             record_pos_,
             avatar,
             filepath_);
}

static
void
block_offset_printer(const std::string          &filepath_,
                     const TDO::DirectoryRecord &record_,
                     const uint32_t              record_pos_,
                     TDO::DevStream             &stream_)
{
  char dir_char;
  char readonly_char;
  char for_fs_char;
  std::uint32_t record_block;
  std::uint32_t avatar;

  dir_char      = (record_.is_directory() ? 'd' : '-');
  readonly_char = (record_.is_readonly() ? 'r' : '-');
  for_fs_char   = (record_.is_for_fs() ? 'f' : '-');

  // Defensive: a malformed image can produce device_block_size() == 0
  // (volume_block_size on disc was 0). Listing is a lenient inspection
  // path -- warn, display 0, continue rather than SIGFPE on
  // integer-divide-by-zero.
  const u64 dev_block_size = stream_.device_block_size();
  if(dev_block_size == 0)
    {
      fmt::print(stderr,
                 "3dt: warning: device_block_size is 0 for {}, "
                 "displaying 0 for record block index\n",
                 filepath_);
      record_block = 0u;
    }
  else
    {
      // record_pos_ is u32 and dev_block_size is u64, so the quotient
      // is bounded by record_pos_ -- a direct narrow back to u32 is
      // safe.
      record_block =
        static_cast<u32>(static_cast<u64>(record_pos_) / dev_block_size);
    }

  // Mirror file_offset_printer's empty-avatar guard: indexing [0] on
  // an empty std::vector is UB. The listing path tolerates a record
  // with no avatars by displaying 0.
  if(record_.avatar_list.empty())
    avatar = 0u;
  else
    avatar = record_.avatar_list[0];

  fmt::print("{}{}{} {:11L} {:#010x} {:4s} {:#010x} {:#010x} {}\n",
             dir_char,
             readonly_char,
             for_fs_char,
             record_.byte_count,
             record_.unique_identifier,
             record_.type_str(),
             record_block,
             avatar,
             filepath_);
}

class ListCallbacks final : public TDO::FSWalker::Callbacks
{
public:
  ListCallbacks(const Options::List &opts_)
    : base_filter(opts_.prefix_filter)
  {
    _header  = ::default_header;
    _printer = ::default_printer;

    if(opts_.format == "file-offsets")
      {
        _header  = ::offset_header;
        _printer = ::file_offset_printer;
      }
    else if(opts_.format == "block-offsets")
      {
        _header  = ::offset_header;
        _printer = ::block_offset_printer;
      }
  }

public:
  void
  begin()
  {
    _header();
  }

  void
  end()
  {
  }

public:
  void
  operator()(const std::filesystem::path&,
             const TDO::DirectoryHeader&,
             TDO::DevStream&)
  {
  }

  void
  operator()(const std::filesystem::path &filepath_,
             const TDO::DirectoryRecord  &record_,
             const uint32_t               record_pos_,
             TDO::DevStream              &stream_)
  {
    if(!starts_with(base_filter,filepath_))
      return;

    _printer(filepath_.generic_string(),record_,record_pos_,stream_);
  }

  Error
  invalid_filename(const std::filesystem::path &parent_,
                   const std::string           &filename_,
                   const TDO::DirectoryRecord  &record_,
                   const uint32_t               record_pos_,
                   const Error                 &err_,
                   TDO::DevStream              &stream_)
  {
    std::string filepath;

    if(!starts_with(base_filter,parent_))
      return Error();

    filepath = display_path(parent_,filename_);
    _printer(filepath,record_,record_pos_,stream_);
    fmt::print(stderr,"3dt: {} - {}\n",err_.str,filepath);

    return Error();
  }

public:
  fs::path base_filter;
  Printer _printer;
  std::function<void()> _header;
};


namespace Subcommand
{
  void
  list(const Options::List &opts_)
  {
    std::fstream fs;
    ListCallbacks callbacks(opts_);
    TDO::FSWalker walker(fs,callbacks);

    fs.open(opts_.filepath,std::ios::binary|std::ios::in);
    if(!fs.good())
      {
        Log::error_stream_open(opts_.filepath);
        throw Error("list failed");
      }

    walker.walk();

    fs.close();
  }
}
