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

#include "subcmd.hpp"
#include "temp_path.hpp"
#include "tdo_disc_unpacker.hpp"
#include "tdo_file_stream.hpp"

#include "fmt.hpp"
#include "options.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  static
  std::string
  label_string(const std::array<char,32> &arr_)
  {
    const char *begin;
    const char *end;

    begin = arr_.data();
    end = static_cast<const char*>(memchr(begin,'\0',arr_.size()));
    if(end == nullptr)
      end = begin + arr_.size();

    return std::string(begin,end);
  }

  static
  fs::path
  create_temp_dir(const fs::path &base_)
  {
    std::random_device rd;
    const std::string stem = base_.stem().string();
    for(std::uint32_t attempt = 0; attempt < 1000; attempt++)
      {
        fs::path tmp = fs::temp_directory_path() /
          fmt::format("3dt-repack-{:08x}-{:08x}-{}",rd(),rd(),stem);
        std::error_code ec;
        if(fs::create_directory(tmp,ec))
          return tmp;
        if(ec && !fs::exists(tmp))
          throw Error("failed to create temporary directory: " +
                      tmp.string() + ": " + ec.message());
      }
    throw Error("failed to find a unique temporary directory name");
  }

  // FileExtractor used to override before()/after() to write each
  // file body to _dstpath. That duplicated TDO::DiscUnpacker::Impl's
  // own extraction (Impl reopens the same path with std::ios::trunc
  // and rewrites the same bytes the callback just wrote), giving
  // every repack a 2x I/O cost and silently undoing any
  // transformation a callback might apply. The unpacker now
  // provides no-op defaults for before()/after(), so repack just
  // needs a default Callback to drive the walk; Impl handles the
  // actual extraction.

  static
  void
  repack_one(const fs::path        &input_,
             const fs::path        &target_,
             const Options::Repack &opts_)
  {
    TDO::DiscLabel disc_label;

    {
      TDO::FileStream stream;
      stream.open(input_);
      disc_label = stream.disc_label();
      stream.close();
    }

    fs::path temp_dir;
    fs::path temp_output;

    try
      {
        temp_dir = create_temp_dir(input_);

        {
          std::fstream ifs;
          ifs.open(input_,std::ios::binary|std::ios::in);
          // Default Callback (all hooks are no-op virtuals); Impl
          // handles the directory-tree creation and file extraction.
          TDO::DiscUnpacker::Callback cb;
          TDO::DiscUnpacker unpacker(ifs,cb);
          unpacker.unpack(temp_dir);
        }

        Options::Pack pack_opts{};
        pack_opts.input = temp_dir;
        pack_opts.output = target_;
        pack_opts.banner_romtag = opts_.banner_romtag;
        pack_opts.billstuff_romtag = opts_.billstuff_romtag;
        pack_opts.mark = opts_.mark;
        pack_opts.sign = opts_.sign;
        pack_opts.signature_digest_check_count = opts_.signature_digest_check_count;

        pack_opts.volume_commentary = label_string(disc_label.volume_commentary);
        pack_opts.volume_label = label_string(disc_label.volume_identifier);
        pack_opts.volume_unique_identifier = disc_label.volume_unique_identifier;
        pack_opts.volume_unique_identifier_set = true;
        pack_opts.root_unique_identifier = disc_label.root_unique_identifier;
        pack_opts.root_unique_identifier_set = true;

        Subcmd::pack(pack_opts);
      }
    catch(...)
      {
        std::error_code ec;
        fs::remove_all(temp_dir,ec);
        throw;
      }

    {
      std::error_code ec;
      fs::remove_all(temp_dir,ec);
      if(ec)
        fmt::print(stderr,
                   "3dt: warning: failed to clean up temp dir {}: {}\n",
                   temp_dir.string(),ec.message());
    }
  }
}

namespace Subcmd
{
  void
  repack(const Options::Repack &opts_)
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
            repack_one(filepath,target,opts_);
          }
        catch(const std::exception &e)
          {
            fmt::print(stderr,"3dt: {} - {}\n",e.what(),filepath);
            failed = true;
          }
      }

    if(failed)
      throw Error("repack failed");
  }
}
