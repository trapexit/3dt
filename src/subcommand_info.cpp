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
#include "error_unknown_image_format.hpp"
#include "log.hpp"
#include "options.hpp"
#include "tdo_file_stream.hpp"
#include "tdo_filesystem_stats.hpp"

#include "CSVWriter.h"

#include "fmt.hpp"

#include <fstream>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

typedef std::function<void(const fs::path&,
                           const TDO::DiscLabel&,
                           const uint32_t,
                           const uint32_t)> PrintFunc;

namespace
{
  static
  void
  print_human(const fs::path       &filepath_,
              const TDO::DiscLabel &label_,
              const uint32_t        file_count_,
              const uint32_t        total_data_size_)
  {
    fmt::print("{}:\n"
               " - record_type: 0x{:02X}\n"
               " - volume_sync_bytes: 0x{:02X}\n"
               " - volume_structure_version: 0x{:02X}\n"
               " - volume_flags: 0x{:02X}\n"
               " - volume_commentary: {}\n"
               " - volume_identifier: {}\n"
               " - volume_unique_identifier: 0x{:08X}\n"
               " - volume_block_size: {}\n"
               " - volume_block_count: {}\n"
               " - root_unique_identifier: 0x{:08X}\n"
               " - root_directory_block_count: {}\n"
               " - root_directory_block_size: {}\n"
               " - root_directory_last_avatar_index: {}\n"
               " - root_directory_avatar_list:\n",
               filepath_.filename(),
               label_.record_type,
               label_.volume_sync_bytes[0],
               label_.volume_structure_version,
               label_.volume_flags,
               &label_.volume_commentary[0],
               &label_.volume_identifier[0],
               label_.volume_unique_identifier,
               label_.volume_block_size,
               label_.volume_block_count,
               label_.root_unique_identifier,
               label_.root_directory_block_count,
               label_.root_directory_block_size,
               label_.root_directory_last_avatar_index);
    for(unsigned int i = 0; i <= label_.root_directory_last_avatar_index; i++)
      fmt::print("   - {}\n",label_.root_directory_avatar_list[i]);
    if(label_.volume_flags & VOLUME_FLAG_M2)
      fmt::print(" - num_rom_tags: {}\n"
                 " - application_id: {}\n",
                 label_.num_rom_tags,
                 label_.application_id);
    fmt::print(" - file_count: {}\n"

               " - total_data_size: {}\n",
               file_count_,
               total_data_size_);
  }

  static
  void
  print_csv(const fs::path       &filepath_,
            const TDO::DiscLabel &label_,
            const uint32_t        file_count_,
            const uint32_t        total_data_size_)
  {
    CSVWriter csv(",");

    csv << filepath_.filename().string();
    csv << &label_.volume_commentary[0];
    csv << &label_.volume_identifier[0];
    csv << fmt::format("0x{:08X}",label_.volume_unique_identifier);
    csv << fmt::format("{}",label_.volume_block_size);
    csv << fmt::format("{}",label_.volume_block_count);
    csv << fmt::format("0x{:08X}",label_.root_unique_identifier);
    csv << fmt::format("{}",label_.root_directory_block_count);
    csv << fmt::format("{}",label_.root_directory_block_size);
    csv << fmt::format("{}",label_.root_directory_last_avatar_index);
    csv << fmt::format("{}",file_count_);
    csv << fmt::format("{}",total_data_size_);

    fmt::print("{}\n",csv.toString());
  }

  static
  void
  print_cheader(const fs::path       &filepath_,
                const TDO::DiscLabel &label_,
                const uint32_t        file_count_,
                const uint32_t        total_data_size_)
  {
    CSVWriter csv(",");

    csv << filepath_.filename().stem();
    csv << fs::path(&label_.volume_identifier[0]);
    csv << fmt::format("0x{:08X}",label_.volume_unique_identifier);
    csv << fmt::format("{}",label_.volume_block_count);
    csv << fmt::format("0x{:08X}",label_.root_unique_identifier);
    csv << fmt::format("{}",file_count_);
    csv << fmt::format("{}",total_data_size_);

    fmt::print("{{{}}},\n",csv.toString());
  }

  static
  PrintFunc
  get_print_function(const Options::Info &options_)
  {
    if(options_.format == "human")
      return ::print_human;
    if(options_.format == "csv")
      return ::print_csv;
    if(options_.format == "cheader")
      return ::print_cheader;

    return ::print_human;
  }

  static
  Error
  get_label(TDO::FileStream &stream_,
            TDO::DiscLabel  &label_)
  {
    Error err;

    stream_.data_byte_seek(0);
    stream_.read(label_);

    return {};
  }

  static
  Error
  get_extra_info(TDO::FileStream &stream_,
                 uint32_t        &file_count_,
                 uint32_t        &total_data_size_)
  {
    Error err;
    TDO::FilesystemStats fsstats;

    err = fsstats.collect(stream_);
    if(err)
      return err;

    file_count_      = fsstats.file_count;
    total_data_size_ = fsstats.total_data_size;

    return {};
  }

  static
  Error
  info(const PrintFunc &printfunc_,
       TDO::FileStream &stream_)
  {
    Error err;
    uint32_t file_count;
    uint32_t total_data_size;
    TDO::DiscLabel label;

    err = ::get_label(stream_,label);
    if(err)
      return err;

    file_count = 0;
    total_data_size = 0;
    err = ::get_extra_info(stream_,file_count,total_data_size);
    if(err)
      return err;

    printfunc_(stream_.filepath(),label,file_count,total_data_size);

    return {};
  }

  static
  void
  info(const PrintFunc &printfunc_,
       const fs::path  &filepath_)
  {
    Error err;
    TDO::FileStream stream;

    err = stream.open(filepath_);
    if(err)
      return Log::error(err);

    if(!stream.good())
      return Log::error_stream_open(filepath_);

    err = ::info(printfunc_,stream);
    if(err)
      return Log::error(err);

    stream.close();
  }

}

namespace Subcommand
{
  void
  info(const Options::Info &options_)
  {
    PrintFunc printfunc;

    printfunc = get_print_function(options_);

    for(auto &filepath : options_.filepaths)
      ::info(printfunc,filepath);
  }
}
