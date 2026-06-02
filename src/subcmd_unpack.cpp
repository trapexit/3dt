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

#include "fmt.hpp"
#include "json.hpp"
#include "log.hpp"
#include "options.hpp"
#include "tdo_disc_unpacker.hpp"

#include "CSVWriter.h"

#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  using json = nlohmann::json;

  static constexpr const char *DEFAULT_LAYOUT_FILENAME = "layout.json";

  static
  std::string
  lowercase(const std::string &str_)
  {
    std::string rv;

    rv.reserve(str_.size());
    for(unsigned char c : str_)
      rv.push_back(std::tolower(c));

    return rv;
  }

  static
  bool
  is_default_layout_filename(const fs::path &path_)
  {
    return (path_.parent_path().empty() &&
            (lowercase(path_.filename().string()) == DEFAULT_LAYOUT_FILENAME));
  }

  static
  std::string
  array_string(const std::array<char,32> &arr_)
  {
    const char *begin;
    const char *end;

    begin = &arr_[0];
    end = static_cast<const char*>(memchr(begin,'\0',arr_.size()));
    if(end == nullptr)
      end = begin + arr_.size();

    return std::string(begin,end);
  }

  static
  std::string
  bytes_hex(const char *data_,
            const u64   size_)
  {
    std::string rv;

    rv.reserve(size_ * 2);
    for(u64 i = 0; i < size_; i++)
      rv += fmt::format("{:02X}",static_cast<unsigned char>(data_[i]));

    return rv;
  }

  static
  std::string
  fixed_string(const char *data_,
               const u64   size_)
  {
    const char *end;

    end = static_cast<const char*>(memchr(data_,'\0',size_));
    if(end == nullptr)
      end = data_ + size_;

    return std::string(data_,end);
  }

  static
  json
  avatar_list_json(const std::vector<u32> &avatar_list_)
  {
    json avatars = json::array();

    for(auto avatar : avatar_list_)
      avatars.emplace_back(avatar);

    return avatars;
  }

  static
  json
  disc_label_json(const TDO::DiscLabel &label_)
  {
    json sync = json::array();
    json root_avatars = json::array();

    for(auto c : label_.volume_sync_bytes)
      sync.emplace_back(static_cast<u8>(c));
    // Cap the loop by the avatar_list's fixed capacity rather than
    // trusting root_directory_last_avatar_index. A malformed/odd label
    // can have last_avatar_index >= 8 (OOB read on the on-disc array)
    // or UINT32_MAX (the original `i <= last_idx` form looped forever
    // once unsigned i wrapped past UINT_MAX, with each iteration
    // emplace_back'ing an OOB value into the JSON manifest until
    // allocation failed). Mirrors the protective shape applied to
    // subcmd_info.cpp's equivalent loop. Cast through u64 so the
    // `+ 1` cannot itself wrap when last_idx is UINT32_MAX.
    const auto avatar_capacity = label_.root_directory_avatar_list.size();
    const u32  last_idx        = label_.root_directory_last_avatar_index;
    const bool last_idx_oob =
      (static_cast<u64>(last_idx) + 1 > avatar_capacity);
    const u32 emit_count =
      last_idx_oob
      ? static_cast<u32>(avatar_capacity)
      : (last_idx + 1);
    for(u32 i = 0; i < emit_count; i++)
      root_avatars.emplace_back(label_.root_directory_avatar_list[i]);
    if(last_idx_oob)
      fmt::print(stderr,
                 "3dt: warning: root_directory_last_avatar_index={} exceeds "
                 "avatar_list capacity {} -- truncating in manifest\n",
                 last_idx,avatar_capacity);

    return {
      {"record_type",label_.record_type},
      {"volume_sync_bytes",sync},
      {"volume_structure_version",label_.volume_structure_version},
      {"volume_flags",label_.volume_flags},
      {"volume_commentary",array_string(label_.volume_commentary)},
      {"volume_identifier",array_string(label_.volume_identifier)},
      {"volume_unique_identifier",label_.volume_unique_identifier},
      {"volume_block_size",label_.volume_block_size},
      {"volume_block_count",label_.volume_block_count},
      {"root_unique_identifier",label_.root_unique_identifier},
      {"root_directory_block_count",label_.root_directory_block_count},
      {"root_directory_block_size",label_.root_directory_block_size},
      {"root_directory_last_avatar_index",label_.root_directory_last_avatar_index},
      {"root_directory_avatar_list",root_avatars}
    };
  }

  static
  json
  romtags_json(TDO::DevStream &stream_)
  {
    json tags = json::array();

    for(const auto &tag : stream_.romtags())
      {
        tags.push_back({
          {"sub_systype",tag.sub_systype},
          {"type",tag.type},
          {"type_name",tag.type_str()},
          {"version",tag.version},
          {"revision",tag.revision},
          {"flags",tag.flags},
          {"type_specific",tag.type_specific},
          {"reserved1",tag.reserved1},
          {"reserved2",tag.reserved2},
          {"offset",tag.offset},
          {"size",tag.size},
          {"reserved3",{tag.reserved3[0],tag.reserved3[1],tag.reserved3[2],tag.reserved3[3]}}
        });
      }

    return tags;
  }

  struct CSVPrinter final : public TDO::DiscUnpacker::Callback
  {
    void
    before(const fs::path             &path_,
           const TDO::DirectoryRecord &record_,
           const uint32_t,
           TDO::DevStream&)
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
    after(const fs::path&,
          const TDO::DirectoryRecord&,
          const int)
    {

    }
  };

  struct HumanPrinter final : public TDO::DiscUnpacker::Callback
  {
    HumanPrinter()
    {
      fmt::print("Flags      Size         ID Type  RecOffset     Avatar Filename\n");
    }

    void
    before(const fs::path             &filepath_,
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
          // must fit in u32. Compute in s64 so we can detect overflow,
          // then narrow. Throwing here surfaces the malformed image
          // rather than silently truncating the displayed offset.
          const s64 file_offset =
            stream_.data_block_to_file_offset(record_.avatar_list[0]);
          if((file_offset < 0) ||
             (file_offset > static_cast<s64>(std::numeric_limits<std::uint32_t>::max())))
            throw Error("avatar file offset out of 32-bit range: " +
                        filepath_.string());
          avatar = static_cast<std::uint32_t>(file_offset);
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

    void
    after(const fs::path&,
          const TDO::DirectoryRecord&,
          const int)
    {

    }
  };

  struct LayoutWriter final : public TDO::DiscUnpacker::Callback
  {
    LayoutWriter(TDO::DiscUnpacker::Callback::Ptr printer_)
      : _printer(std::move(printer_)),
        _initialized(false)
    {
    }

    void
    init(TDO::DevStream &stream_)
    {
      if(_initialized)
        return;

      _initialized = true;
      _manifest = {
        {"format","3dt-operafs-layout"},
        {"version",1},
        {"image",{
          {"container",stream_.device_block_header() == 0 ? "iso2048" : "mode1_2352"},
          {"file_size",stream_.size_in_bytes()},
          {"data_start_offset",stream_.data_start_offset()},
          {"device_block_size",stream_.device_block_size()},
          {"device_block_header",stream_.device_block_header()},
          {"device_block_data_size",stream_.device_block_data_size()},
          {"device_block_footer",stream_.device_block_footer()},
          {"disc_label_block",stream_.disc_label_block()},
          {"romtags_block",stream_.romtags_block()}
        }},
        {"disc_label",disc_label_json(stream_.disc_label())},
        {"rom_tags",romtags_json(stream_)},
        {"entries",json::array()}
      };
    }

    void
    directory(const fs::path              &path_,
              const TDO::DirectoryHeader &header_,
              const uint32_t              header_pos_,
              TDO::DevStream             &stream_)
    {
      (void)path_;
      (void)header_;
      (void)header_pos_;
      init(stream_);
    }

    void
    before(const fs::path             &path_,
           const TDO::DirectoryRecord &record_,
           const uint32_t              record_pos_,
           TDO::DevStream             &stream_)
    {
      init(stream_);
      _printer->before(path_,record_,record_pos_,stream_);
      if(!record_.is_directory() && is_default_layout_filename(path_))
        _default_layout_payload_paths.emplace_back(path_);
      _manifest["entries"].push_back({
        {"path",path_.generic_string()},
        {"kind",record_.is_directory() ? "directory" : "file"},
        {"record_file_offset",record_pos_},
        {"record_data_offset",stream_.data_byte_tell(record_pos_)},
        {"record_size",68 + (record_.avatar_list.size() * sizeof(u32))},
        {"flags",record_.flags},
        {"unique_identifier",record_.unique_identifier},
        {"type",record_.type},
        {"block_size",record_.block_size},
        {"byte_count",record_.byte_count},
        {"block_count",record_.block_count},
        {"burst",record_.burst},
        {"gap",record_.gap},
        {"filename",fixed_string(record_.filename,sizeof(record_.filename))},
        {"filename_raw_hex",bytes_hex(record_.filename,sizeof(record_.filename))},
        {"last_avatar_index",record_.last_avatar_index},
        {"avatar_list",avatar_list_json(record_.avatar_list)},
        {"start_block",record_.avatar_list.empty() ? 0 : record_.avatar_list[0]}
      });
    }

    void
    after(const fs::path             &path_,
          const TDO::DirectoryRecord &record_,
          const int                   err_)
    {
      _printer->after(path_,record_,err_);
    }

    void
    end()
    {
      _printer->end();
    }

    void
    write(const fs::path &layout_path_)
    {
      std::ofstream os;

      os.open(layout_path_,std::ios::trunc);
      if(!os)
        throw Error("failed to open layout output file: " + layout_path_.string());

      os << _manifest.dump(2) << '\n';
      if(!os)
        throw Error("failed to write layout output file: " + layout_path_.string());
      os.close();
      if(os.fail())
        throw Error("failed to close layout output file: " + layout_path_.string());
    }

    bool
    default_layout_payload_conflicts(const fs::path &dstpath_,
                                     const fs::path &layout_path_) const
    {
      for(const auto &path : _default_layout_payload_paths)
        {
          std::error_code ec;
          const fs::path extracted_path = dstpath_ / path;

          if(extracted_path == layout_path_)
            return true;

          const bool equivalent = fs::equivalent(extracted_path,layout_path_,ec);
          if(!ec && equivalent)
            return true;
        }

      return false;
    }

  private:
    TDO::DiscUnpacker::Callback::Ptr _printer;
    json                             _manifest;
    std::vector<fs::path>            _default_layout_payload_paths;
    bool                             _initialized;
  };

  TDO::DiscUnpacker::Callback::Ptr
  get_printer(const std::string &format_)
  {
    if(format_ == "csv")
      return std::make_unique<CSVPrinter>();

    return std::make_unique<HumanPrinter>();
  }

  static
  fs::path
  layout_path_for(const Options::Unpack &options_,
                  const fs::path        &dstpath_)
  {
    if(!options_.layout.empty())
      return options_.layout;

    return (dstpath_ / DEFAULT_LAYOUT_FILENAME);
  }
}

namespace Subcmd
{
  void
  unpack(const Options::Unpack &options_)
  {
    bool failed;
    fs::path dstpath;

    if(!options_.layout.empty() && (options_.filepaths.size() != 1))
      {
        Log::error({"--layout requires exactly one input image"});
        throw Error("unpack failed");
      }

    failed = false;
    for(auto &srcpath : options_.filepaths)
      {
        fs::path layout_path;
        TDO::DiscUnpacker::Ptr unpacker;
        TDO::DiscUnpacker::Callback::Ptr printer;
        LayoutWriter *layout_writer;
        std::fstream fs;

        fs.open(srcpath,fs.binary|fs.in);
        if(!fs.is_open())
          {
            Log::error_stream_open(srcpath);
            failed = true;
            continue;
          }

        if(options_.output.empty())
          dstpath = srcpath.string() + ".unpacked";
        else
          dstpath = options_.output;

        fs::create_directories(dstpath);

        layout_path = layout_path_for(options_,dstpath);

        printer = get_printer(options_.format);
        {
          auto lw = std::make_unique<LayoutWriter>(std::move(printer));
          layout_writer = lw.get();
          printer = std::move(lw);
        }
        unpacker = std::make_unique<TDO::DiscUnpacker>(fs,*printer);

        try
          {
            unpacker->unpack(dstpath);
            if(options_.layout.empty() &&
               layout_writer->default_layout_payload_conflicts(dstpath,layout_path))
              {
                throw Error("default layout output conflicts with extracted file: " +
                            layout_path.string() +
                            "; pass --layout outside the unpacked root");
              }
            layout_writer->write(layout_path);
          }
        catch(const std::exception &e)
          {
            Log::error({e.what()});
            failed = true;
          }

        fs.close();
      }

    if(failed)
      throw Error("unpack failed");
  }
}
