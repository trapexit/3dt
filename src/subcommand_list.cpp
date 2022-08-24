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

class ListCallbacks final : public TDO::FSWalker::Callbacks
{
public:
  void
  operator()(const std::filesystem::path &filepath_,
             const TDO::DirectoryRecord  &record_,
             TDO::DevStream              &stream_)
  {
    char dir_char;
    char readonly_char;
    char for_fs_char;

    if(!starts_with(base_filter,filepath_))
      return;

    dir_char      = (record_.is_directory() ? 'd' : '-');
    readonly_char = (record_.is_readonly() ? 'r' : '-');
    for_fs_char   = (record_.is_for_fs() ? 'f' : '-');
    fmt::print("{}{}{} {:11L} {:#010x} {:#010x} ({:4s}) {}\n",
               dir_char,
               readonly_char,
               for_fs_char,
               record_.byte_count,
               record_.unique_identifier,
               record_.type,
               record_.type_str(),
               filepath_);
  }

public:
  fs::path base_filter;
};


namespace Subcommand
{
  void
  list(const Options::List &options_)
  {
    Error err;
    std::ifstream ifs;
    ListCallbacks callbacks;
    TDO::FSWalker walker(ifs,callbacks);

    callbacks.base_filter = options_.prefix_filter;

    ifs.open(options_.filepath,std::ios::binary);
    if(!ifs.good())
      return  Log::error_stream_open(options_.filepath);

    err = walker.walk();
    if(err)
      Log::error(err);

    ifs.close();
  }
}
