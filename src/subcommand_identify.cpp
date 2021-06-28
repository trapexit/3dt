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


#include "CSVWriter.h"

#include "error.hpp"
#include "error_unknown_image_format.hpp"
#include "log.hpp"
#include "options.hpp"
#include "tdo_disc_ids.hpp"
#include "tdo_disc_reader.hpp"
#include "tdo_disc_identifier.hpp"

#include "fmt.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <vector>

namespace fs = std::filesystem;

struct PrintData
{
  fs::path filename;
  TDO::DiscLabel label;
  uint32_t file_count;
  uint32_t total_data_size;
  TDO::IDVec full_matches;
  TDO::IDVec partial_matches;
};

typedef std::function<void(const PrintData&)> PrintFunc;

namespace
{
  static
  void
  print_human(const PrintData &data_)
  {
    fmt::print("{}:\n"
                " - volume_unique_identifier: 0x{:08X}\n"
                " - volume_block_count: {}\n"
                " - root_unique_identifier: 0x{:08X}\n"
                " - file_count: {}\n"
                " - total_data_size: {}\n",
                data_.filename,
                data_.label.volume_unique_identifier,
                data_.label.volume_block_count,
                data_.label.root_unique_identifier,
                data_.file_count,
                data_.total_data_size);

    if(data_.full_matches.empty() && data_.partial_matches.empty())
      {
        fmt::print(" - no_matches:\n"
                   "   - Please file a ticket at\n"
                   "   - https://github.com/trapexit/3dt/issues\n"
                   "   - with the details above.\n");
        return;
      }

    if(!data_.full_matches.empty())
      {
        fmt::print(" - matches:\n");
        for(auto id : data_.full_matches)
          fmt::print("   - {}\n",id->name);
      }

    if(!data_.partial_matches.empty())
      {
        fmt::print(" - partial_matches:\n");
        for(auto id : data_.partial_matches)
          fmt::print(stdout,"   - {}\n",id->name);
      }
  }

  static
  void
  print_csv_base(const PrintData &data_,
                 CSVWriter       &csv_)
  {
    csv_ << data_.filename.string();
    csv_ << fmt::format("0x{:08X}",data_.label.volume_unique_identifier);
    csv_ << data_.label.volume_block_count;
    csv_ << fmt::format("0x{:08X}",data_.label.root_unique_identifier);
    csv_ << data_.file_count;
    csv_ << data_.total_data_size;
  }

  static
  void
  print_csv_no_matches(const PrintData &data_)
  {
    CSVWriter csv(",");

    print_csv_base(data_,csv);
    csv << "no_match"
        << "file ticket at https://github.com/trapexit/3dt/issues";

    fmt::format("{}\n",csv.toString());
  }

  static
  void
  print_csv_matches(const PrintData &data_)
  {
    for(auto id : data_.full_matches)
      {
        CSVWriter csv(",");

        print_csv_base(data_,csv);
        csv << "full"
            << id->name;

        fmt::print("{}\n",csv.toString());
      }

    for(auto id : data_.partial_matches)
      {
        CSVWriter csv(",");

        print_csv_base(data_,csv);
        csv << "partial"
            << id->name;

        fmt::print("{}\n",csv.toString());
      }
  }

  static
  void
  print_csv(const PrintData &data_)
  {
    if(data_.full_matches.empty() && data_.partial_matches.empty())
      return print_csv_no_matches(data_);
    return print_csv_matches(data_);
  }

  static
  Error
  identify(const PrintFunc &printfunc_,
           const fs::path  &filepath_,
           std::istream    &is_)
  {
    Error err;
    PrintData data;
    TDO::DiscIdentifier identifier;

    err = identifier.identify(is_);
    if(err)
      return err;

    data.filename        = filepath_;
    data.label           = identifier.label;
    data.file_count      = identifier.fsstats.file_count;
    data.total_data_size = identifier.fsstats.total_data_size;
    data.full_matches    = identifier.full_matches;
    data.partial_matches = identifier.partial_matches;

    printfunc_(data);

    return {};
  }

  static
  void
  identify(const PrintFunc &printfunc_,
           const fs::path  &filepath_)
  {
    Error err;
    std::ifstream ifs;

    ifs.open(filepath_,std::ios::binary);
    if(!ifs.good())
      return Log::error_stream_open(filepath_);

    err = ::identify(printfunc_,filepath_,ifs);
    if(err)
      Log::error(err);

    ifs.close();
  }

  static
  PrintFunc
  get_print_func(const std::string &format_)
  {
    if(format_ == "human")
      return ::print_human;
    if(format_ == "csv")
      return ::print_csv;

    return ::print_human;
  }
}

namespace Subcommand
{
  void
  identify(const Options::Identify &options_)
  {
    PrintFunc printfunc;

    printfunc = get_print_func(options_.format);

    for(auto &filepath : options_.filepaths)
      ::identify(printfunc,filepath);
  }
}
