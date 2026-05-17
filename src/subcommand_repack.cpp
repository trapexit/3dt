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
#include "temp_path.hpp"
#include "tdo_disc_unpacker.hpp"
#include "tdo_file_stream.hpp"

#include "copy_stream.hpp"
#include "fmt.hpp"
#include "options.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace
{
  static
  std::string
  label_string(const std::array<char,32> &arr_)
  {
    return std::string(arr_.data());
  }

  static
  fs::path
  create_temp_dir(const fs::path &base_)
  {
    std::random_device rd;
    fs::path tmp = fs::temp_directory_path() /
      ("3dt-repack-" + std::to_string(rd()) + "-" + base_.stem().string());
    fs::create_directories(tmp);
    return tmp;
  }

  class FileExtractor final : public TDO::DiscUnpacker::Callback
  {
  public:
    FileExtractor(const fs::path &dstpath_)
      : _dstpath(dstpath_)
    {
    }

    void
    before(const fs::path             &path_,
           const TDO::DirectoryRecord &record_,
           const uint32_t              record_pos_,
           TDO::DevStream             &stream_) override
    {
      (void)record_pos_;

      fs::path fullpath = _dstpath / path_;
      if(record_.is_directory())
        {
          fs::create_directories(fullpath);
        }
      else
        {
          std::ofstream os;
          std::uint32_t bytes_left;

          os.open(fullpath,std::ios::binary|std::ios::trunc);

          bytes_left = record_.byte_count;
          for(auto avatar : record_.avatar_list)
            {
              if(bytes_left == 0)
                break;

              std::uint32_t sector_end = avatar + record_.block_count;
              for(std::uint32_t sector = avatar; sector < sector_end; sector++)
                {
                  std::uint32_t n;
                  stream_.data_block_seek(sector);
                  n = std::min(record_.block_size,bytes_left);
                  util::copy_stream(stream_.iostream(),os,n);
                  bytes_left -= n;
                }
            }
          os.close();
        }
    }

    void
    after(const fs::path             &,
          const TDO::DirectoryRecord &,
          const int                   err_) override
    {
      (void)err_;
    }

  private:
    fs::path _dstpath;
  };

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
          FileExtractor extractor(temp_dir);
          TDO::DiscUnpacker unpacker(ifs,extractor);
          unpacker.unpack(temp_dir);
        }

        Options::Pack pack_opts{};
        pack_opts.input = temp_dir;
        pack_opts.output = target_;
        pack_opts.banner_romtag = opts_.banner_romtag;
        pack_opts.mark = opts_.mark;
        pack_opts.sign = opts_.sign;
        pack_opts.signature_digest_check_count = opts_.signature_digest_check_count;

        pack_opts.volume_commentary = label_string(disc_label.volume_commentary);
        pack_opts.volume_label = label_string(disc_label.volume_identifier);
        pack_opts.volume_unique_identifier = disc_label.volume_unique_identifier;
        pack_opts.volume_unique_identifier_set = true;
        pack_opts.root_unique_identifier = disc_label.root_unique_identifier;
        pack_opts.root_unique_identifier_set = true;

        Subcommand::pack(pack_opts);
      }
    catch(...)
      {
        std::error_code ec;
        fs::remove_all(temp_dir,ec);
        throw;
      }

    fs::remove_all(temp_dir);
  }
}

namespace Subcommand
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
