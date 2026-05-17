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

#include "subcommand.hpp"
#include "tdo_disc_signer.hpp"

#include "fmt.hpp"
#include "options.hpp"
#include "types_ints.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace
{
  static
  fs::path
  temp_path_for(const fs::path &path_)
  {
    for(u32 i = 0; i < 1000; i++)
      {
        fs::path path;

        path = path_;
        path += fmt::format(".tmp{}",i);
        if(!fs::exists(path))
          return path;
      }

    throw std::runtime_error("failed to create temporary output path");
  }

  static
  fs::path
  backup_path_for(const fs::path &path_)
  {
    fs::path path;

    path = path_;
    path += ".bak";
    return path;
  }

  static
  void
  verify_signed_image(const fs::path &path_)
  {
    Options::Verify verify_opts{};

    verify_opts.filepaths.emplace_back(path_);
    Subcommand::verify(verify_opts);
  }

  static
  bool
  is_signatures_file_too_small(const Error &err_)
  {
    return (err_.str.find("signatures file too small") != std::string::npos);
  }

  static
  std::string
  sign_error_message(const Error &err_)
  {
    if(is_signatures_file_too_small(err_))
      return (err_.str +
              "; unpack and repack the image with 3dt pack --sign to rebuild signing space");

    return err_.str;
  }

  static
  void
  sign_one(const fs::path     &input_,
           const fs::path     &target_,
           const Options::Sign &opts_)
  {
    Error err;
    fs::path temp_path;

    if(!opts_.output.empty() && fs::exists(target_))
      throw std::runtime_error("output already exists: " + target_.string());

    temp_path = temp_path_for(target_);
    try
      {
        const auto digest_check_count =
          static_cast<std::uint8_t>(opts_.signature_digest_check_count);

        fs::copy_file(input_,temp_path,fs::copy_options::none);

        err = TDO::sign_disc_image(temp_path,
                                   opts_.mark,
                                   !opts_.force,
                                   opts_.banner_romtag,
                                   digest_check_count);
        if(err)
          throw std::runtime_error(sign_error_message(err));

        verify_signed_image(temp_path);

        if(opts_.backup && opts_.output.empty())
          fs::copy_file(input_,backup_path_for(input_),fs::copy_options::none);

        fs::rename(temp_path,target_);
      }
    catch(...)
      {
        std::error_code ec;

        fs::remove(temp_path,ec);
        throw;
      }
  }
}

namespace Subcommand
{
  void
  sign(const Options::Sign &opts_)
  {
    bool failed;

    failed = false;
    if(!opts_.output.empty() && (opts_.filepaths.size() != 1))
      throw Error("--output requires exactly one input image");

    for(const auto &filepath : opts_.filepaths)
      {
        fs::path target;

        target = opts_.output.empty() ? filepath : opts_.output;
        try
          {
            sign_one(filepath,target,opts_);
          }
        catch(const std::exception &e)
          {
            fmt::print(stderr,"3dt: {} - {}\n",e.what(),filepath);
            failed = true;
          }
      }

    if(failed)
      throw Error("signing failed");
  }
}
