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

#include "fmt.hpp"
#include "log.hpp"
#include "options.hpp"
#include "tdo_disc_unpacker.hpp"

#include "CSVWriter.h"

#include <fstream>

namespace fs = std::filesystem;

namespace
{
  struct CSVPrinter final : public TDO::DiscUnpacker::Callback
  {
    void
    before(const fs::path             &path_,
           const TDO::DirectoryRecord &record_)
    {
      CSVWriter csv(",");

      csv << path_.string();
      csv << fmt::format("0x{:08X}",record_.type);
      csv << fmt::format("{}",record_.type_str());
      csv << fmt::format("0x{:08X}",record_.unique_identifier);
      csv << fmt::format("0x{:08X}",record_.disc_avatar_offset());
      csv << record_.byte_count;

      fmt::print("{}\n",csv.toString());
    }

    void
    after(const fs::path             &path_,
          const TDO::DirectoryRecord &record_,
          const int                   err_)
    {

    }
  };

  struct HumanPrinter final : public TDO::DiscUnpacker::Callback
  {
    void
    before(const fs::path             &filepath_,
           const TDO::DirectoryRecord &record_)
    {
      char dir_char;
      char readonly_char;
      char for_fs_char;

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

    void
    after(const fs::path             &filepath_,
          const TDO::DirectoryRecord &record_,
          const int                   err_)
    {

    }
  };

  TDO::DiscUnpacker::Callback::Ptr
  get_printer(const std::string &format_)
  {
    if(format_ == "human")
      return std::make_unique<HumanPrinter>();
    if(format_ == "csv")
      return std::make_unique<CSVPrinter>();

    return std::make_unique<HumanPrinter>();
  }
}

namespace Subcommand
{
  void
  unpack(const Options::Unpack &options_)
  {
    Error err;
    fs::path dstpath;
    std::ifstream ifs;
    TDO::DiscUnpacker::Ptr unpacker;
    TDO::DiscUnpacker::Callback::Ptr printer;

    printer  = get_printer(options_.format);
    unpacker = std::make_unique<TDO::DiscUnpacker>(ifs,*printer);

    for(auto &srcpath : options_.filepaths)
      {
        ifs.open(srcpath,std::ios::binary);
        if(ifs.bad())
          {
            Log::error_stream_open(srcpath);
            continue;
          }

        dstpath = options_.output / (srcpath.filename().concat(".unpacked"));

        err = unpacker->unpack(dstpath);
        if(err)
          Log::error(err);

        ifs.close();
      }
  }
}
