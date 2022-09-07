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

#include "log.hpp"
#include "options.hpp"
#include "tdo_fs_walker.hpp"

#include "fmt.hpp"

#include <fstream>

namespace fs = std::filesystem;
typedef std::function<void(const fs::path&,const TDO::DirectoryRecord&,const uint32_t,TDO::DevStream&)> Printer;


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
void
default_header()
{
  fmt::print("Flags      Size         ID Type Filename\n");
}

static
void
default_printer(const fs::path             &filepath_,
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
file_offset_printer(const fs::path             &filepath_,
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
  avatar = (stream_.data_offset() + (record_.avatar_list[0] * stream_.device_block_size()));
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
block_offset_printer(const fs::path             &filepath_,
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
  fmt::print("{}{}{} {:11L} {:#010x} {:4s} {:#010x} {:#010x} {}\n",
             dir_char,
             readonly_char,
             for_fs_char,
             record_.byte_count,
             record_.unique_identifier,
             record_.type_str(),
             record_pos_ / stream_.device_block_size(),
             record_.avatar_list[0],
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

public:
  void
  operator()(const std::filesystem::path &filepath_,
             const TDO::DirectoryRecord  &record_,
             const uint32_t               record_pos_,
             TDO::DevStream              &stream_)
  {
    if(!starts_with(base_filter,filepath_))
      return;

    _printer(filepath_,record_,record_pos_,stream_);
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
    Error err;
    std::ifstream ifs;
    ListCallbacks callbacks(opts_);
    TDO::FSWalker walker(ifs,callbacks);

    ifs.open(opts_.filepath,std::ios::binary);
    if(!ifs.good())
      return  Log::error_stream_open(opts_.filepath);

    err = walker.walk();
    if(err)
      Log::error(err);

    ifs.close();
  }
}
