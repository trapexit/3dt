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

#include "error.hpp"
#include "log.hpp"
#include "options.hpp"
#include "tdo_disc_ids.h"
#include "tdo_disc_identifier.hpp"

#include "fmt.hpp"

#include <fstream>

namespace fs = std::filesystem;

namespace l
{
  static
  Error
  rename(const fs::path    &filepath_,
         const TDO::ID     *id_,
         const std::string &ext_)
  {
    std::error_code ec;
    fs::path newfilepath;

    newfilepath = filepath_;
    newfilepath.replace_filename(id_->name);
    newfilepath.replace_extension(ext_);

    std::filesystem::rename(filepath_,newfilepath,ec);
    if(ec)
      return fmt::format("error renaming {} -> {}",filepath_,newfilepath);

    fmt::print("{}: renamed to {}\n",filepath_,newfilepath);

    return {};
  }

  static
  Error
  rename(const fs::path &filepath_,
         const bool      take_first_,
         std::istream   &is_)
  {
    Error err;
    TDO::DiscIdentifier identifier;

    err = identifier.identify(is_);
    if(err)
      return err;

    if(identifier.full_matches.empty())
      return {"no match found"};

    if((identifier.full_matches.size() == 1) || (take_first_ == true))
      return l::rename(filepath_,identifier.full_matches[0],identifier.disc_image_ext);

    fmt::print("{}: skipping due to multiple matches - ",filepath_);
    for(auto match : identifier.full_matches)
      fmt::print("{}",match->name);
    fmt::print("\n");

    return {};
  }

  static
  void
  rename(const fs::path &filepath_,
         const bool      take_first_)
  {
    Error err;
    std::ifstream ifs;

    ifs.open(filepath_,std::ios::binary);
    if(!ifs.good())
      {
        Log::error_stream_open(filepath_);
        return;
      }

    err = l::rename(filepath_,take_first_,ifs);
    if(err)
      Log::error(err);

    ifs.close();
  }
}

namespace Subcommand
{
  void
  rename(const Options::Rename &options_)
  {
    for(auto &filepath : options_.filepaths)
      l::rename(filepath,options_.take_first);
  }
}
