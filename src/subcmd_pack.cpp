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

#include "log.hpp"
#include "fmt.hpp"
#include "json.hpp"
#include "options.hpp"
#include "subcmd.hpp"
#include "temp_path.hpp"
#include "tdo_directory_record.hpp"
#include "tdo_disc_format.hpp"
#include "tdo_disc_label.hpp"
#include "tdo_disc_packer.hpp"
#include "tdo_disc_signer.hpp"
#include "tdo_file_stream.hpp"
#include "tdo_fs_walker.hpp"
#include "tdo_rsa.h"
#include "tdo_safe_narrow.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  using json = nlohmann::json;
  using Entry = TDO::DiscManifestEntry;
  using EntryKind = TDO::DiscManifestEntryKind;

  static constexpr u32 FIRST_FILE_BLOCK = 2;
  static constexpr const char *DEFAULT_LAYOUT_FILENAME = "layout.json";

  struct LayoutRecord
  {
    std::string path;
    u32         flags;
    u32         unique_identifier;
    u32         type;
    u32         block_size;
    u32         byte_count;
    u32         block_count;
    u32         burst;
    u32         gap;
    u32         start_block;
    u32         record_file_offset;
    u32         record_size;
    std::vector<u32> avatar_list;
    u32         order;
  };

  struct AllocatedRange
  {
    u64         start_block;
    u64         end_block;
    std::string label;
    std::string special_key;
    bool        implicit;
  };

  typedef std::unordered_map<std::string,LayoutRecord> LayoutMap;

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
  std::string
  path_key(const fs::path &path_)
  {
    return lowercase(path_.lexically_normal().generic_string());
  }

  static
  std::string
  display_path(const fs::path &path_)
  {
    std::string path;

    path = path_.generic_string();
    return path.empty() ? std::string("/") : path;
  }

  static
  std::string
  special_key(const fs::path &path_)
  {
    std::string key;

    key = path_key(path_);
    if((key == "disc label") || (key == "rom_tags"))
      return key;

    return {};
  }

  static
  bool
  is_unpacked_metadata_file(const fs::path &path_)
  {
    const std::string key = lowercase(path_.filename().string());

    return ((key == DEFAULT_LAYOUT_FILENAME) ||
            (key == "disc label") ||
            (key == "rom_tags") ||
            (key == "signatures"));
  }

  static
  std::string
  range_string(const AllocatedRange &range_)
  {
    if(range_.end_block == (range_.start_block + 1))
      return std::to_string(range_.start_block);

    return (std::to_string(range_.start_block) + "-" +
            std::to_string(range_.end_block - 1));
  }

  static
  bool
  same_name(const Entry      &entry_,
            const char *const name_)
  {
    return (lowercase(entry_.name) == name_);
  }

  static
  u32
  random_unique_identifier()
  {
    std::random_device random_device;
    std::uniform_int_distribution<u32> distribution(1,std::numeric_limits<u32>::max());

    return distribution(random_device);
  }

  static
  void
  apply_pack_unique_identifiers(const Options::Pack &options_,
                                TDO::DiscManifest  &manifest_,
                                const bool          preserve_unspecified_ = false)
  {
    if(!preserve_unspecified_ || options_.volume_unique_identifier_set)
      manifest_.disc_label.volume_unique_identifier =
        options_.volume_unique_identifier != 0 ?
        options_.volume_unique_identifier : random_unique_identifier();
    if(!preserve_unspecified_ || options_.root_unique_identifier_set)
      manifest_.disc_label.root_unique_identifier =
        options_.root_unique_identifier != 0 ?
        options_.root_unique_identifier : random_unique_identifier();
    manifest_.root.unique_identifier = manifest_.disc_label.root_unique_identifier;
  }

  static
  u32
  block_count_for_size(u64 size_,
                       u32 block_size_ = TDO::BLOCK_SIZE)
  {
    if(size_ == 0)
      return 0;
    if(block_size_ == 0)
      throw Error("block size must be non-zero");

    return TDO::checked_narrow_u64_to_u32(((size_ + block_size_ - 1) / block_size_),
                                          "block count");
  }

  static
  u32
  physical_block_count(const Entry &entry_)
  {
    u64 bytes;

    if(entry_.block_count == 0)
      return 0;
    if(entry_.block_size == 0)
      throw Error("block size must be non-zero");

    bytes = static_cast<u64>(entry_.block_count) * entry_.block_size;

    return TDO::checked_narrow_u64_to_u32(((bytes + TDO::BLOCK_SIZE - 1) / TDO::BLOCK_SIZE),
                                          "physical block count");
  }

  static
  u32
  type_from_string(const std::string &str_)
  {
    u32 type;
    std::string padded;

    type = 0;
    padded = str_;
    padded.resize(4,' ');
    for(char c : padded)
      type = ((type << 8) | static_cast<u8>(c));

    return type;
  }

  static
  u32
  file_type(const fs::path &path_)
  {
    std::string ext;

    ext = path_.extension().string();
    if(ext.empty())
      return 0;
    if(ext[0] == '.')
      ext.erase(ext.begin());
    if(ext.empty())
      return 0;
    if(ext.size() > 4)
      ext.resize(4);

    return type_from_string(ext);
  }

  static
  bool
  is_supported_source(const fs::directory_entry &entry_)
  {
    return (entry_.is_directory() || entry_.is_regular_file());
  }

  static
  void
  reject_symlink(const fs::directory_entry &entry_)
  {
    std::error_code ec;
    const bool is_link = fs::is_symlink(entry_.path(),ec);
    // Match reject_symlink_path: fs::is_symlink(path, ec) returns false
    // in both the "not a symlink" and "status query failed" cases.
    // Treat any error as fail-closed to avoid silently passing a real
    // symlink whose stat() failed for a permission/race reason. The
    // throwing directory_entry::is_symlink() overload would surface a
    // generic filesystem_error for the same condition.
    if(ec)
      throw Error("failed to determine symlink status of pack source: " +
                  entry_.path().string() + ": " + ec.message());
    if(is_link)
      throw Error("symlinks are not supported as pack sources: " +
                  entry_.path().string());
  }

  static
  void
  reject_symlink_path(const fs::path &path_)
  {
    std::error_code ec;
    const bool is_link = fs::is_symlink(path_,ec);
    // fs::is_symlink(path, ec) returns false in both the "not a
    // symlink" and "status query failed" cases. Treat any error as
    // fail-closed to avoid silently passing a real symlink whose
    // stat() failed for a permission/race reason.
    if(ec)
      throw Error("failed to determine symlink status of pack source: " +
                  path_.string() + ": " + ec.message());
    if(is_link)
      throw Error("symlinks are not supported as pack sources: " +
                  path_.string());
  }

  static
  std::string
  printable_filename(const std::string &name_)
  {
    std::string out;
    out.reserve(name_.size());
    for(char c : name_)
      {
        const unsigned char uc = static_cast<unsigned char>(c);
        if(uc < 0x20 || uc == 0x7f)
          {
            char buf[5];
            std::snprintf(buf,sizeof(buf),"\\x%02x",uc);
            out.append(buf);
          }
        else
          {
            out.push_back(c);
          }
      }
    return out;
  }

  static
  void
  validate_filename(const std::string &name_)
  {
    if(name_.empty())
      throw Error("empty filename is not supported");
    if(name_.size() >= FILESYSTEM_MAX_NAME_LEN)
      throw Error("filename is too long for OperaFS: " + printable_filename(name_));
    if((name_ == ".") || (name_ == ".."))
      throw Error("reserved filename is not supported: " + name_);
    for(char c : name_)
      {
        if(c == '/' || c == '\\')
          throw Error("filename contains path separator: " + printable_filename(name_));
      }
  }

  static
  std::unique_ptr<Entry>
  make_entry(const fs::directory_entry &dirent_)
  {
    auto entry = std::make_unique<Entry>();

    entry->src_path           = dirent_.path();
    entry->name               = dirent_.path().filename().string();
    entry->kind               = EntryKind::Normal;
    entry->directory          = dirent_.is_directory();
    entry->unique_identifier  = 0;
    entry->type               = (entry->directory ? DR_TYPE_DIRECTORY : file_type(dirent_.path()));
    entry->flags              = DR_FLAG_IS_READONLY;
    entry->block_size         = TDO::BLOCK_SIZE;
    entry->byte_count         = 0;
    entry->data_byte_count    = 0;
    entry->block_count        = 0;
    entry->burst              = 0;
    entry->gap                = 0;
    entry->start_block        = 0;
    entry->record_file_offset = 0;
    entry->record_size        = 0;

    validate_filename(entry->name);

    if(entry->directory)
      {
        entry->flags |= (DR_FLAG_IS_DIRECTORY | DR_FLAG_IS_FOR_FILESYSTEM);
      }
    else
      {
        u64 size;
        std::error_code ec;

        size = fs::file_size(dirent_.path(),ec);
        if(ec)
          throw Error("failed to stat input file: " +
                      dirent_.path().string() + ": " + ec.message());
        entry->byte_count      = TDO::checked_narrow_u64_to_u32(size,"file size");
        entry->data_byte_count = entry->byte_count;
        entry->block_count     = block_count_for_size(size);
        if(lowercase(entry->name) == "launchme")
          entry->type = DR_TYPE_CATAPULT;
      }

    return entry;
  }

  static
  void
  read_directory(const fs::path &path_,
                 Entry         &parent_,
                 bool           root_ = true)
  {
    std::vector<fs::directory_entry> dirents;

    for(const auto &dirent : fs::directory_iterator(path_))
      {
        reject_symlink(dirent);
        if(root_ && dirent.is_regular_file() && is_unpacked_metadata_file(dirent.path()))
          continue;
        if(!is_supported_source(dirent))
          throw Error("unsupported filesystem entry: " + dirent.path().string());
        dirents.emplace_back(dirent);
      }

    std::sort(dirents.begin(),
              dirents.end(),
              [](const fs::directory_entry &lhs_,
                 const fs::directory_entry &rhs_)
              {
                return lhs_.path().filename() < rhs_.path().filename();
              });

    for(const auto &dirent : dirents)
      {
        auto entry = make_entry(dirent);

        if(entry->directory)
          read_directory(dirent.path(),*entry,false);

        parent_.children.emplace_back(std::move(entry));
      }
  }

  static
  Entry*
  find_root_child(Entry       &root_,
                  const char  *name_)
  {
    for(auto &entry : root_.children)
      {
        if(same_name(*entry,name_))
          return entry.get();
      }

    return nullptr;
  }

  static
  Entry&
  add_root_child(Entry       &root_,
                 const char  *name_)
  {
    auto entry = std::make_unique<Entry>();

    entry->name               = name_;
    entry->kind               = EntryKind::Normal;
    entry->directory          = false;
    entry->unique_identifier  = 0;
    entry->type               = 0;
    entry->flags              = DR_FLAG_IS_READONLY;
    entry->block_size         = TDO::BLOCK_SIZE;
    entry->byte_count         = 0;
    entry->data_byte_count    = 0;
    entry->block_count        = 0;
    entry->burst              = 0;
    entry->gap                = 0;
    entry->start_block        = 0;
    entry->record_file_offset = 0;
    entry->record_size        = 0;

    root_.children.emplace_back(std::move(entry));

    return *root_.children.back();
  }

  static
  void
  setup_disc_label_entry(Entry &entry_)
  {
    entry_.name            = "Disc label";
    entry_.kind            = EntryKind::DiscLabel;
    entry_.directory       = false;
    entry_.type            = DR_TYPE_LABEL;
    entry_.flags           = (DR_FLAG_IS_READONLY | DR_FLAG_IS_FOR_FILESYSTEM);
    entry_.block_size      = TDO::BLOCK_SIZE;
    entry_.byte_count      = sizeof(TDO::DiscLabel);
    entry_.data_byte_count = 0;
    entry_.block_count     = 1;
    entry_.burst           = 0;
    entry_.gap             = 0;
    entry_.start_block     = 0;
    entry_.avatar_list     = {0};
    entry_.children.clear();
  }

  static
  void
  setup_romtags_entry(Entry &entry_)
  {
    entry_.name            = "rom_tags";
    entry_.kind            = EntryKind::ROMTags;
    entry_.directory       = false;
    entry_.type            = 0;
    entry_.flags           = DR_FLAG_IS_READONLY;
    entry_.block_size      = TDO::BLOCK_SIZE;
    entry_.byte_count      = 0;
    entry_.data_byte_count = 0;
    entry_.block_count     = 1;
    entry_.burst           = 0;
    entry_.gap             = 0;
    entry_.start_block     = 1;
    entry_.avatar_list     = {1};
    entry_.children.clear();
  }

  static
  void
  setup_signatures_entry(Entry &entry_)
  {
    entry_.name            = "signatures";
    entry_.kind            = EntryKind::Signatures;
    entry_.directory       = false;
    entry_.type            = 0;
    entry_.flags           = DR_FLAG_IS_READONLY;
    entry_.block_size      = TDO::BLOCK_SIZE;
    entry_.data_byte_count = 0;
    entry_.burst           = 0;
    entry_.gap             = 0;
    entry_.children.clear();
  }

  static
  void
  sort_root_entries(Entry &root_)
  {
    std::sort(root_.children.begin(),
              root_.children.end(),
              [](const std::unique_ptr<Entry> &lhs_,
                 const std::unique_ptr<Entry> &rhs_)
              {
                if(lhs_->kind == EntryKind::DiscLabel)
                  return true;
                if(rhs_->kind == EntryKind::DiscLabel)
                  return false;
                return (lhs_->name < rhs_->name);
              });
  }

  static
  void
  add_or_replace_synthetic_entries(Entry &root_,
                                   bool   reserve_signing_space_)
  {
    Entry *entry;

    entry = find_root_child(root_,"disc label");
    if(entry == nullptr)
      entry = &add_root_child(root_,"Disc label");
    setup_disc_label_entry(*entry);

    entry = find_root_child(root_,"rom_tags");
    if(entry == nullptr)
      entry = &add_root_child(root_,"rom_tags");
    setup_romtags_entry(*entry);

    if(reserve_signing_space_)
      {
        entry = find_root_child(root_,"signatures");
        if(entry == nullptr)
          entry = &add_root_child(root_,"signatures");
        setup_signatures_entry(*entry);
      }

    sort_root_entries(root_);
  }

  static
  void
  assign_unique_identifiers(Entry &entry_,
                            u32   &next_id_)
  {
    for(auto &child : entry_.children)
      {
        child->unique_identifier = next_id_++;
        if(child->directory)
          assign_unique_identifiers(*child,next_id_);
      }
  }

  static
  u32
  parse_u32(const std::string &str_,
            const char        *label_)
  {
    char *end;
    unsigned long value;

    end = nullptr;
    value = std::strtoul(str_.c_str(),&end,0);
    if((end == str_.c_str()) || (*end != '\0'))
      throw Error(std::string("invalid layout ") + label_ + ": " + str_);
    if(value > std::numeric_limits<u32>::max())
      throw Error(std::string("layout ") + label_ + " is too large");

    return static_cast<u32>(value);
  }

  static
  u32
  json_u32(const json &json_,
           const char *label_)
  {
    if(json_.is_string())
      return parse_u32(json_.get<std::string>(),label_);
    // nlohmann/json's get<u32> truncates silently when the JSON value
    // exceeds UINT32_MAX. Route both signed and unsigned integer
    // representations through the canonical checked-narrow helpers so
    // overflow throws with the offending value in the message, matching
    // the string path's parse_u32 behavior. Reject non-integer types
    // explicitly so floats / nulls / objects don't slip through as 0.
    if(json_.is_number_unsigned())
      return TDO::checked_narrow_u64_to_u32(json_.get<u64>(),
                                            std::string("layout ") + label_);
    if(json_.is_number_integer())
      return TDO::checked_narrow_s64_to_u32(json_.get<s64>(),
                                            std::string("layout ") + label_);
    throw Error(std::string("layout ") + label_ + " is not an integer");
  }

  static
  u32
  json_u32_value(const json &json_,
                 const char *key_,
                 u32         default_)
  {
    if(!json_.contains(key_))
      return default_;
    return json_u32(json_.at(key_),key_);
  }

  static
  std::vector<u32>
  json_u32_vec(const json &json_)
  {
    std::vector<u32> rv;

    for(const auto &value : json_)
      rv.emplace_back(json_u32(value,"avatar_list"));

    return rv;
  }

  static
  void
  read_fixed_string(const json        &json_,
                    const char        *key_,
                    std::array<char,32> &dst_)
  {
    std::string value;

    dst_.fill(0);
    value = json_.value(key_,std::string());
    memcpy(&dst_[0],value.c_str(),std::min<std::size_t>(value.size(),dst_.size() - 1));
  }

  static
  void
  apply_layout_disc_label(TDO::DiscManifest &manifest_,
                          const json        &layout_)
  {
    const json &label = layout_.at("disc_label");

    manifest_.disc_label.record_type =
      TDO::checked_narrow_u32_to_u8(json_u32_value(label,"record_type",RECORD_STD_VOLUME),
                                    "record_type");
    manifest_.disc_label.volume_sync_bytes.fill(VOLUME_SYNC_BYTE);
    if(label.contains("volume_sync_bytes"))
      {
        auto values = json_u32_vec(label.at("volume_sync_bytes"));
        if(values.size() > manifest_.disc_label.volume_sync_bytes.size())
          throw Error("layout volume_sync_bytes is too long");
        for(std::size_t i = 0; i < values.size(); i++)
          {
            // VSBArray slots are char (8-bit). The JSON values pass
            // through json_u32_vec, so silently truncating here would
            // mask out-of-range layout entries. Reject them instead.
            if(values[i] > std::numeric_limits<uint8_t>::max())
              throw Error("layout volume_sync_bytes value is out of range");
            manifest_.disc_label.volume_sync_bytes[i] = static_cast<char>(values[i]);
          }
      }
    manifest_.disc_label.volume_structure_version =
      TDO::checked_narrow_u32_to_u8(json_u32_value(label,"volume_structure_version",VOLUME_STRUCTURE_OPERA_READONLY),
                                    "volume_structure_version");
    manifest_.disc_label.volume_flags =
      TDO::checked_narrow_u32_to_u8(json_u32_value(label,"volume_flags",0),
                                    "volume_flags");
    read_fixed_string(label,"volume_commentary",manifest_.disc_label.volume_commentary);
    read_fixed_string(label,"volume_identifier",manifest_.disc_label.volume_identifier);
    manifest_.disc_label.volume_unique_identifier = json_u32_value(label,"volume_unique_identifier",0);
    manifest_.disc_label.volume_block_size = json_u32_value(label,"volume_block_size",TDO::BLOCK_SIZE);
    manifest_.disc_label.volume_block_count = json_u32_value(label,"volume_block_count",0);
    manifest_.disc_label.root_unique_identifier = json_u32_value(label,"root_unique_identifier",2);
    manifest_.disc_label.root_directory_block_count = json_u32_value(label,"root_directory_block_count",0);
    manifest_.disc_label.root_directory_block_size = json_u32_value(label,"root_directory_block_size",TDO::BLOCK_SIZE);
    manifest_.disc_label.root_directory_last_avatar_index = json_u32_value(label,"root_directory_last_avatar_index",0);
    if(manifest_.disc_label.root_directory_last_avatar_index >= manifest_.disc_label.root_directory_avatar_list.size())
      throw Error("layout root directory avatar list is too large");
    manifest_.disc_label.root_directory_avatar_list.fill(0);
    if(label.contains("root_directory_avatar_list"))
      {
        auto values = json_u32_vec(label.at("root_directory_avatar_list"));
        for(std::size_t i = 0; (i < values.size()) && (i < manifest_.disc_label.root_directory_avatar_list.size()); i++)
          manifest_.disc_label.root_directory_avatar_list[i] = values[i];
      }

    manifest_.root.unique_identifier = manifest_.disc_label.root_unique_identifier;
    manifest_.root.block_size = manifest_.disc_label.root_directory_block_size;
    manifest_.root.block_count = manifest_.disc_label.root_directory_block_count;
    manifest_.root.byte_count =
      TDO::checked_narrow_u64_to_u32(static_cast<u64>(manifest_.root.block_count) * manifest_.root.block_size,
                                     "root directory byte count");
    manifest_.root.avatar_list.clear();
    for(std::size_t i = 0; i <= manifest_.disc_label.root_directory_last_avatar_index; i++)
      manifest_.root.avatar_list.emplace_back(manifest_.disc_label.root_directory_avatar_list[i]);
    if(!manifest_.root.avatar_list.empty())
      manifest_.root.start_block = manifest_.root.avatar_list[0];
  }

  static
  LayoutMap
  read_layout(const fs::path    &layout_,
              TDO::DiscManifest &manifest_)
  {
    LayoutMap records;
    std::ifstream is;
    u32 order;
    json layout;

    is.open(layout_);
    if(!is)
      throw Error("failed to open layout file: " + layout_.string());

    is >> layout;
    if(layout.value("format",std::string()) != "3dt-operafs-layout")
      throw Error("unsupported layout manifest format");
    if(layout.contains("image") &&
       (layout["image"].value("device_block_data_size",TDO::BLOCK_SIZE) != TDO::BLOCK_SIZE))
      throw Error("packing NVRAM or other non-2048-byte images is not supported yet");
    apply_layout_disc_label(manifest_,layout);
    manifest_.total_blocks = manifest_.disc_label.volume_block_count;
    manifest_.replay_layout = true;

    order = 0;
    for(const auto &entry_json : layout.at("entries"))
      {
        LayoutRecord record;

        record.path               = fs::path(entry_json.at("path").get<std::string>()).lexically_normal().generic_string();
        record.flags              = json_u32(entry_json.at("flags"),"flags");
        record.unique_identifier  = json_u32(entry_json.at("unique_identifier"),"unique_identifier");
        record.type               = json_u32(entry_json.at("type"),"type");
        record.block_size         = json_u32_value(entry_json,"block_size",TDO::BLOCK_SIZE);
        record.byte_count         = json_u32_value(entry_json,"byte_count",0);
        record.block_count        = json_u32_value(entry_json,"block_count",0);
        record.burst              = json_u32_value(entry_json,"burst",0);
        record.gap                = json_u32_value(entry_json,"gap",0);
        record.start_block        = json_u32_value(entry_json,"start_block",0);
        record.record_file_offset = json_u32_value(entry_json,"record_file_offset",0);
        record.record_size        = json_u32_value(entry_json,"record_size",0);
        if(entry_json.contains("avatar_list"))
          record.avatar_list = json_u32_vec(entry_json.at("avatar_list"));
        if(record.avatar_list.empty())
          record.avatar_list = {record.start_block};
        record.start_block = record.avatar_list[0];
        record.order = order++;

        records[path_key(record.path)] = record;
      }

    return records;
  }

  static
  void
  apply_layout_record(Entry              &entry_,
                      const LayoutRecord &record_)
  {
    entry_.unique_identifier  = record_.unique_identifier;
    entry_.type               = record_.type;
    entry_.flags              = (record_.flags & ~DR_FLAG_LAST_IN_MASK);
    entry_.block_size         = record_.block_size;
    entry_.byte_count         = record_.byte_count;
    entry_.data_byte_count    = record_.byte_count;
    entry_.block_count        = record_.block_count;
    entry_.burst              = record_.burst;
    entry_.gap                = record_.gap;
    entry_.start_block        = record_.start_block;
    entry_.record_file_offset = record_.record_file_offset;
    entry_.record_size        = record_.record_size;
    entry_.avatar_list        = record_.avatar_list;
    if(entry_.directory)
      entry_.flags |= DR_FLAG_IS_DIRECTORY;
    else
      entry_.flags &= ~DR_FLAG_IS_DIRECTORY;
  }

  static
  u32
  layout_order(const Entry     &entry_,
               const fs::path  &path_,
               const LayoutMap &layout_)
  {
    u32 order;

    order = std::numeric_limits<u32>::max();

    auto it = layout_.find(path_key(path_));
    if(it != layout_.end())
      order = std::min(order,it->second.order);

    for(const auto &child : entry_.children)
      order = std::min(order,layout_order(*child,path_ / child->name,layout_));

    return order;
  }

  static
  bool
  apply_layout(Entry           &entry_,
               const fs::path  &entry_path_,
               const LayoutMap &layout_)
  {
    std::vector<Entry::Ptr> children;

    for(auto &child : entry_.children)
      {
        fs::path child_path;
        bool keep;

        child_path = entry_path_ / child->name;

        auto it = layout_.find(path_key(child_path));
        keep = (it != layout_.end());
        if(keep)
          apply_layout_record(*child,it->second);

        if(child->directory)
          keep = (apply_layout(*child,child_path,layout_) || keep);

        if(keep)
          children.emplace_back(std::move(child));
      }

    std::vector<std::pair<u32,Entry::Ptr>> ordered;
    ordered.reserve(children.size());
    for(auto &child : children)
      ordered.emplace_back(layout_order(*child,entry_path_ / child->name,layout_),
                             std::move(child));

    std::sort(ordered.begin(),
              ordered.end(),
              [](const auto &lhs_,
                 const auto &rhs_)
              {
                if(lhs_.first != rhs_.first)
                  return (lhs_.first < rhs_.first);
                return (lhs_.second->name < rhs_.second->name);
              });

    children.clear();
    for(auto &p : ordered)
      children.emplace_back(std::move(p.second));

    entry_.children = std::move(children);

    return !entry_.children.empty();
  }

  static
  void
  validate_replay_file_sizes(Entry          &entry_,
                             const fs::path &entry_path_)
  {
    if((entry_.block_count > 0) && (entry_.block_size == 0))
      throw Error("layout entry has zero block size: " +
                               display_path(entry_path_));

    if(entry_.directory)
      {
        u32 required_blocks;

        required_blocks = TDO::directory_block_count(entry_);
        if(entry_.block_count < required_blocks)
          throw Error("layout directory allocation too small: " +
                                   display_path(entry_path_) + " needs " +
                                   std::to_string(required_blocks) +
                                   " blocks but has " +
                                   std::to_string(entry_.block_count));
        entry_.byte_count = TDO::checked_narrow_u64_to_u32(static_cast<u64>(entry_.block_count) * entry_.block_size,
                                                           "directory byte count");
      }
    else if(entry_.kind == EntryKind::Normal)
      {
        u64 capacity;
        u64 source_size;
        u32 required_blocks;
        std::error_code ec;

        if(entry_.src_path.empty())
          throw Error("layout entry has no source file: " + entry_path_.generic_string());

        source_size = fs::file_size(entry_.src_path,ec);
        if(ec)
          throw Error("failed to stat layout source file: " +
                      entry_.src_path.string() + " (" +
                      display_path(entry_path_) + "): " + ec.message());
        capacity = static_cast<u64>(entry_.block_count) * entry_.block_size;
        if(source_size > capacity)
          throw Error("layout byte count exceeds allocation: " +
                                   display_path(entry_path_));
        required_blocks = block_count_for_size(source_size,entry_.block_size);
        if(entry_.block_count < required_blocks)
          throw Error("layout file allocation too small: " +
                                   display_path(entry_path_) + " needs " +
                                   std::to_string(required_blocks) +
                                   " blocks but has " +
                                   std::to_string(entry_.block_count));

        entry_.byte_count = TDO::checked_narrow_u64_to_u32(source_size,"file byte count");
        entry_.data_byte_count = entry_.byte_count;
      }

    for(auto &child : entry_.children)
      validate_replay_file_sizes(*child,entry_path_ / child->name);
  }

  static
  void
  compute_directory_sizes(Entry &entry_)
  {
    if(!entry_.directory)
      return;

    entry_.block_count = TDO::directory_block_count(entry_);
    entry_.byte_count  = TDO::checked_narrow_u64_to_u32(static_cast<u64>(entry_.block_count) * TDO::BLOCK_SIZE,
                                                        "directory byte count");

    for(auto &child : entry_.children)
      compute_directory_sizes(*child);
  }

  static
  u32
  sum_directory_blocks(const Entry &entry_)
  {
    u64 blocks;

    blocks = (entry_.directory ? entry_.block_count : 0);
    for(const auto &child : entry_.children)
      blocks += sum_directory_blocks(*child);

    return TDO::checked_narrow_u64_to_u32(blocks,"sum of directory blocks");
  }

  static
  u32
  sum_file_blocks_without_signatures(const Entry &entry_)
  {
    u64 blocks;

    blocks = 0;
    if(!entry_.directory)
      {
        if((entry_.kind != EntryKind::DiscLabel) &&
           (entry_.kind != EntryKind::ROMTags) &&
           (entry_.kind != EntryKind::Signatures))
          blocks += entry_.block_count;
      }

    for(const auto &child : entry_.children)
      blocks += sum_file_blocks_without_signatures(*child);

    return TDO::checked_narrow_u64_to_u32(blocks,"sum of file blocks");
  }

  static
  u32
  signature_digest_count_for_volume(u32 volume_blocks_)
  {
    u64 padded_blocks;

    padded_blocks   = TDO::round_up(volume_blocks_,(TDO::LOG_BLOCK_SIZE / TDO::BLOCK_SIZE));

    return TDO::checked_narrow_u64_to_u32((padded_blocks * TDO::BLOCK_SIZE) / TDO::LOG_BLOCK_SIZE,"signature digest count");
  }

  static
  u32
  required_signature_file_size_for_volume(u32 volume_blocks_)
  {
    return TDO::checked_narrow_u64_to_u32(TDO::signature_file_size_for_digest_count(signature_digest_count_for_volume(volume_blocks_)),
                                          "signatures file size");
  }

  static
  u32
  required_signature_blocks_for_volume(u32 volume_blocks_)
  {
    return TDO::checked_narrow_u64_to_u32(required_signature_file_size_for_volume(volume_blocks_) / TDO::BLOCK_SIZE,
                                          "signatures file block count");
  }

  static
  u32
  required_signature_blocks(u32 base_blocks_)
  {
    u32 sig_blocks;

    sig_blocks = 1;
    while(true)
      {
        u32 next_sig_blocks;

        next_sig_blocks = required_signature_blocks_for_volume(base_blocks_ + sig_blocks);
        if(next_sig_blocks == sig_blocks)
          return sig_blocks;
        sig_blocks = next_sig_blocks;
      }
  }

  static
  Entry*
  find_signatures_entry(Entry &entry_)
  {
    if(entry_.kind == EntryKind::Signatures)
      return &entry_;

    for(auto &child : entry_.children)
      {
        Entry *rv;

        rv = find_signatures_entry(*child);
        if(rv != nullptr)
          return rv;
      }

    return nullptr;
  }

  static
  void
  reserve_signatures_file(Entry &root_,
                          u32    volume_blocks_ = 0)
  {
    Entry *entry;
    u32 base_blocks;
    u32 num_digests;
    u32 record_size;
    u32 sig_blocks;

    entry = find_signatures_entry(root_);
    if(entry == nullptr)
      throw Error("layout removed required signatures file");

    if(volume_blocks_ != 0)
      {
        num_digests = signature_digest_count_for_volume(volume_blocks_);
        sig_blocks = TDO::checked_narrow_u64_to_u32(TDO::signature_file_size_for_digest_count(num_digests) / TDO::BLOCK_SIZE,
                                                    "signatures file block count");
        record_size = TDO::signature_record_size_for_digest_count(num_digests);
      }
    else
      {
        u64 acc = FIRST_FILE_BLOCK;
        acc += sum_directory_blocks(root_);
        acc += sum_file_blocks_without_signatures(root_);
        base_blocks = TDO::checked_narrow_u64_to_u32(acc,"base block count");
        sig_blocks  = required_signature_blocks(base_blocks);
        num_digests = signature_digest_count_for_volume(
                        TDO::checked_narrow_u64_to_u32(static_cast<u64>(base_blocks) + sig_blocks,
                                                       "volume block count"));
        record_size = TDO::signature_record_size_for_digest_count(num_digests);
      }

    entry->block_count = std::max(entry->block_count,sig_blocks);
    entry->byte_count  = std::max(entry->byte_count,record_size);
  }

  static
  u32
  max_allocated_block(const Entry &entry_)
  {
    u32 max_block;
    u32 physical_blocks;

    max_block = 0;
    physical_blocks = physical_block_count(entry_);
    for(auto avatar : entry_.avatar_list)
      max_block = std::max(max_block,
                           TDO::checked_narrow_u64_to_u32(static_cast<u64>(avatar) + physical_blocks,
                                                          "allocated block"));
    if(entry_.avatar_list.empty() && (entry_.block_count > 0))
      max_block = std::max(max_block,
                           TDO::checked_narrow_u64_to_u32(static_cast<u64>(entry_.start_block) + physical_blocks,
                                                          "allocated block"));
    for(const auto &child : entry_.children)
      max_block = std::max(max_block,max_allocated_block(*child));

    return max_block;
  }

  static
  void
  ensure_replay_signatures_entry(Entry &root_,
                                 u32   &next_id_,
                                 u32    total_blocks_)
  {
    Entry *entry;

    entry = find_signatures_entry(root_);
    if(entry != nullptr)
      return;

    entry = &add_root_child(root_,"signatures");
    setup_signatures_entry(*entry);
    entry->unique_identifier = next_id_++;
    entry->start_block = std::max(total_blocks_,max_allocated_block(root_));
    entry->avatar_list = {entry->start_block};
  }

  static
  std::vector<u32>
  allocated_avatars(const Entry &entry_)
  {
    if(!entry_.avatar_list.empty())
      return entry_.avatar_list;
    if(entry_.block_count > 0)
      return {entry_.start_block};

    return {};
  }

  static
  void
  validate_special_placement(const Entry           &entry_,
                             const fs::path        &entry_path_,
                             const std::vector<u32> &avatars_)
  {
    std::string key;

    key = special_key(entry_path_);
    if(key.empty())
      return;

    if(key == "disc label")
      {
        if((entry_.block_count != 1) || avatars_.empty())
          throw Error("layout cannot resize special file: " +
                                   display_path(entry_path_));
        if(avatars_[0] != 0)
          throw Error("layout cannot relocate Disc label; it must use block 0");
      }
    else if(key == "rom_tags")
      {
        if((entry_.block_count > 1) || avatars_.empty())
          throw Error("layout cannot resize special file: " +
                                   display_path(entry_path_));
        if(avatars_[0] != 1)
          throw Error("layout cannot relocate rom_tags; it must use block 1");
      }
  }

  static
  void
  add_allocated_range(std::vector<AllocatedRange> &ranges_,
                      u64                         start_block_,
                      u64                         block_count_,
                      const std::string          &label_,
                      const std::string          &special_key_,
                      bool                        implicit_)
  {
    u64 end_block;

    if(block_count_ == 0)
      return;

    end_block = start_block_ + block_count_;
    if((end_block < start_block_) || (end_block > std::numeric_limits<u32>::max()))
      throw Error("layout allocation is too large: " + label_);

    ranges_.push_back({start_block_,end_block,label_,special_key_,implicit_});
  }

  static
  void
  collect_allocated_ranges(const Entry                 &entry_,
                           const fs::path              &entry_path_,
                           std::vector<AllocatedRange> &ranges_)
  {
    std::string key;
    std::string label;
    std::vector<u32> avatars;

    key     = special_key(entry_path_);
    label   = display_path(entry_path_);
    avatars = allocated_avatars(entry_);

    validate_special_placement(entry_,entry_path_,avatars);

    // Original 3DO-packed images can contain zero-byte file records with a
    // stale avatar that aliases another file. They have no logical data, so
    // do not treat their block_count as an allocated range.
    if(!entry_.directory &&
       (entry_.kind == EntryKind::Normal) &&
       (entry_.byte_count == 0))
      return;

    for(auto avatar : avatars)
      add_allocated_range(ranges_,
                          avatar,
                          physical_block_count(entry_),
                          label,
                          key,
                          false);

    for(const auto &child : entry_.children)
      collect_allocated_ranges(*child,entry_path_ / child->name,ranges_);
  }

  static
  bool
  same_implicit_special_range(const AllocatedRange &lhs_,
                              const AllocatedRange &rhs_)
  {
    return ((lhs_.implicit != rhs_.implicit) &&
            !lhs_.special_key.empty() &&
            (lhs_.special_key == rhs_.special_key));
  }

  static
  void
  validate_no_block_overlaps(std::vector<AllocatedRange> &ranges_)
  {
    std::sort(ranges_.begin(),
              ranges_.end(),
              [](const AllocatedRange &lhs_,
                 const AllocatedRange &rhs_)
              {
                if(lhs_.start_block != rhs_.start_block)
                  return (lhs_.start_block < rhs_.start_block);
                return (lhs_.end_block < rhs_.end_block);
              });

    for(std::size_t i = 0; i < ranges_.size(); i++)
      {
        for(std::size_t j = i; j > 0; j--)
          {
            const auto &lhs = ranges_[j - 1];
            const auto &rhs = ranges_[i];

            if(lhs.end_block <= rhs.start_block)
              continue;
            if(same_implicit_special_range(lhs,rhs))
              continue;

            throw Error("layout block overlap: " +
                                     lhs.label + " blocks " + range_string(lhs) +
                                     " overlaps " +
                                     rhs.label + " blocks " + range_string(rhs));
          }
      }
  }

  static
  void
  validate_layout_allocations(const Entry &root_)
  {
    std::vector<AllocatedRange> ranges;

    add_allocated_range(ranges,0,1,"Disc label","disc label",true);
    add_allocated_range(ranges,1,1,"rom_tags","rom_tags",true);
    collect_allocated_ranges(root_,fs::path(),ranges);
    validate_no_block_overlaps(ranges);
  }

  static
  void
  allocate_directory_blocks(Entry &entry_,
                            u32   &next_block_)
  {
    if(!entry_.directory)
      return;

    if(entry_.block_count > 0)
      {
        entry_.start_block = next_block_;
        entry_.avatar_list = {entry_.start_block};
        next_block_ = TDO::checked_add_u32(next_block_,
                                           entry_.block_count,
                                           "directory next_block accumulator");
      }

    for(auto &child : entry_.children)
      allocate_directory_blocks(*child,next_block_);
  }

  static
  void
  allocate_file_blocks(Entry &entry_,
                       u32   &next_block_)
  {
    if(!entry_.directory)
      {
        switch(entry_.kind)
          {
          case EntryKind::DiscLabel:
          case EntryKind::ROMTags:
            return;
          case EntryKind::Normal:
          case EntryKind::Signatures:
            break;
          }

        if(entry_.block_count > 0)
          {
            entry_.start_block = next_block_;
            entry_.avatar_list = {entry_.start_block};
            next_block_ = TDO::checked_add_u32(next_block_,
                                               entry_.block_count,
                                               "file next_block accumulator");
          }
      }

    for(auto &child : entry_.children)
      allocate_file_blocks(*child,next_block_);
  }

  static
  u32
  allocate_blocks(Entry &root_)
  {
    u32 next_block;

    next_block = FIRST_FILE_BLOCK;
    allocate_directory_blocks(root_,next_block);
    allocate_file_blocks(root_,next_block);

    return next_block;
  }

  static
  fs::path
  canonicalize_for_containment(const fs::path &path_)
  {
    std::error_code ec;
    // Absolutize first so weakly_canonical operates on an absolute path
    // even when the input is a bare relative filename (e.g. `-o out.iso`).
    // libstdc++'s weakly_canonical preserves a relative input verbatim
    // when no on-disk prefix exists, which would let
    // validate_output_location's iterator-prefix check silently miss
    // containment violations (the relative output starts with "out.iso"
    // while the input canonicalizes to "/abs/path/...", and the loop
    // returns on the first segment mismatch).
    fs::path absolute = fs::absolute(path_,ec);
    if(ec || absolute.empty())
      throw Error("failed to absolutize path for containment check: " +
                  path_.string() +
                  (ec ? (": " + ec.message()) : std::string()));
    fs::path canonical = fs::weakly_canonical(absolute,ec);
    // Fail closed. Falling back to lexically_normal on the unresolved
    // path would let a symlinked input or an EACCES-on-traverse hide
    // input/output overlap from validate_output_location, and an empty
    // canonical_ would bypass the prefix check entirely (the loop runs
    // zero iterations and the function returns silently).
    if(ec || canonical.empty())
      throw Error("failed to canonicalize path for containment check: " +
                  path_.string() +
                  (ec ? (": " + ec.message()) : std::string()));
    return canonical;
  }

  static
  void
  validate_output_location(const fs::path &input_,
                           const fs::path &output_)
  {
    fs::path input_abs;
    fs::path output_abs;

    input_abs  = canonicalize_for_containment(input_);
    output_abs = canonicalize_for_containment(output_);

    auto input_it = input_abs.begin();
    auto output_it = output_abs.begin();
    while((input_it != input_abs.end()) && (output_it != output_abs.end()))
      {
        if(*input_it != *output_it)
          return;
        ++input_it;
        ++output_it;
      }

    if(input_it == input_abs.end())
      throw Error("output image cannot be inside the input directory");
  }

  static
  fs::path
  layout_path_for(const Options::Pack &options_)
  {
    fs::path layout;

    if(!options_.layout.empty())
      return options_.layout;

    layout = options_.input / DEFAULT_LAYOUT_FILENAME;
    if(!fs::exists(layout))
      return {};
    if(!fs::is_regular_file(layout))
      throw Error("layout path is not a regular file: " + layout.string());

    std::error_code ec;
    const std::uintmax_t sz = fs::file_size(layout,ec);
    if(ec)
      throw Error("failed to stat layout file: " +
                  layout.string() + ": " + ec.message());
    if(sz == 0)
      {
        // Auto-discovered layout file (no --layout argument). A
        // 0-byte file most often comes from a killed unpack/repack
        // that wrote the path before the body. Restore the prior
        // behavior of treating that as "no layout, fresh pack" so
        // the user is not forced to manually rm the stale file
        // before pack will run, but warn so the stale file does not
        // go unnoticed.
        fmt::print(stderr,
                   "3dt: warning: ignoring empty auto-discovered layout file: {}\n",
                   layout.string());
        return {};
      }

    return layout;
  }



  static
  void
  preflight_input_files(const Entry &entry_)
  {
    if(!entry_.directory && (entry_.kind == EntryKind::Normal))
      {
        std::ifstream is;

        is.open(entry_.src_path,std::ios::binary);
        if(!is)
          throw Error("failed to open input file: " + entry_.src_path.string());
      }

    for(const auto &child : entry_.children)
      preflight_input_files(*child);
  }

  static
  void
  verify_operafs_structure_file(const fs::path &path_)
  {
    TDO::FSWalker::Callbacks callbacks;
    TDO::FileStream stream;

    stream.open(path_);

    TDO::FSWalker fsw(stream,callbacks);
    fsw.walk();
  }

  static
  void
  list_packed_file(const std::string          &filepath_,
                   const TDO::DirectoryRecord &record_)
  {
    char dir_char      = (record_.is_directory() ? 'd' : '-');
    char readonly_char = (record_.is_readonly() ? 'r' : '-');
    char for_fs_char   = (record_.is_for_fs() ? 'f' : '-');

    fmt::print("{}{}{} {:11L} {:#010x} {:4s} {}\n",
               dir_char,
               readonly_char,
               for_fs_char,
               record_.byte_count,
               record_.unique_identifier,
               record_.type_str(),
               filepath_);
  }

  class PackListCallbacks final : public TDO::FSWalker::Callbacks
  {
  public:
    void
    begin()
    {
      fmt::print("Flags      Size         ID Type Filename\n");
    }

    void
    operator()(const std::filesystem::path &,
               const TDO::DirectoryHeader &,
               TDO::DevStream &)
    {
    }

    void
    operator()(const std::filesystem::path &filepath_,
               const TDO::DirectoryRecord  &record_,
               const uint32_t,
               TDO::DevStream &)
    {
      list_packed_file(filepath_.generic_string(),record_);
    }

    Error
    invalid_filename(const std::filesystem::path &parent_,
                     const std::string           &filename_,
                     const TDO::DirectoryRecord  &record_,
                     const uint32_t,
                     const Error                 &,
                     TDO::DevStream &)
    {
      std::string filepath = display_path(parent_);
      if(!filepath.empty() && filepath != "/")
        filepath += "/";
      filepath += filename_;
      list_packed_file(filepath,record_);
      return Error();
    }
  };

  static
  void
  list_packed_image(const fs::path &path_)
  {
    TDO::FileStream stream;

    stream.open(path_);

    PackListCallbacks callbacks;
    TDO::FSWalker fsw(stream,callbacks);

    fmt::print("{}:\n  - Contents:\n",path_);
    fsw.walk();
  }

  static
  TDO::DiscManifest
  create_manifest(const Options::Pack &options_)
  {
    LayoutMap layout;
    TDO::DiscManifest manifest{};
    fs::path layout_path;
    u32 next_id;

    validate_output_location(options_.input,options_.output);
    reject_symlink_path(options_.input);

    manifest.output = options_.output;
    manifest.disc_label = {};
    manifest.disc_label.record_type = RECORD_STD_VOLUME;
    manifest.disc_label.volume_sync_bytes.fill(VOLUME_SYNC_BYTE);
    manifest.disc_label.volume_structure_version = VOLUME_STRUCTURE_OPERA_READONLY;
    manifest.disc_label.volume_flags = 0;
    if(options_.volume_commentary.size() >= VOLUME_COM_LEN)
      throw Error("volume commentary is too long for OperaFS");
    memcpy(&manifest.disc_label.volume_commentary[0],
           options_.volume_commentary.c_str(),
           options_.volume_commentary.size());
    if(options_.volume_label.size() >= VOLUME_ID_LEN)
      throw Error("volume label is too long for OperaFS");
    memcpy(&manifest.disc_label.volume_identifier[0],
           options_.volume_label.c_str(),
           options_.volume_label.size());
    manifest.disc_label.volume_block_size = TDO::BLOCK_SIZE;
    manifest.disc_label.root_directory_block_size = TDO::BLOCK_SIZE;
    manifest.total_blocks = 0;
    manifest.replay_layout = false;
    manifest.root.name = "";
    manifest.root.kind = EntryKind::Normal;
    manifest.root.directory = true;
    apply_pack_unique_identifiers(options_,manifest);
    manifest.root.type = DR_TYPE_DIRECTORY;
    manifest.root.flags = (DR_FLAG_IS_DIRECTORY |
                           DR_FLAG_IS_READONLY |
                           DR_FLAG_IS_FOR_FILESYSTEM);
    manifest.root.block_size = TDO::BLOCK_SIZE;
    manifest.root.byte_count = 0;
    manifest.root.block_count = 0;
    manifest.root.burst = 0;
    manifest.root.gap = 0;
    manifest.root.start_block = 0;
    manifest.root.record_file_offset = 0;
    manifest.root.record_size = 0;

    layout_path = layout_path_for(options_);
    if(!layout_path.empty())
      layout = read_layout(layout_path,manifest);

    read_directory(options_.input,manifest.root);
    add_or_replace_synthetic_entries(manifest.root,true);

    next_id = 2;
    assign_unique_identifiers(manifest.root,next_id);

    if(!layout_path.empty())
      {
        apply_layout(manifest.root,fs::path(),layout);
        apply_pack_unique_identifiers(options_,manifest,true);
        ensure_replay_signatures_entry(manifest.root,
                                       next_id,
                                       manifest.total_blocks);
      }

    if(manifest.replay_layout)
      {
        {
          u32 total_blocks;

          do
            {
              total_blocks = manifest.total_blocks;
              reserve_signatures_file(manifest.root,manifest.total_blocks);
              manifest.total_blocks = std::max(manifest.total_blocks,
                                               max_allocated_block(manifest.root));
            }
          while(manifest.total_blocks != total_blocks);
        }
        validate_replay_file_sizes(manifest.root,fs::path());
        validate_layout_allocations(manifest.root);
      }
    else
      {
        compute_directory_sizes(manifest.root);
        reserve_signatures_file(manifest.root);
        manifest.total_blocks = allocate_blocks(manifest.root);
      }

    return manifest;
  }
}

namespace Subcmd
{
  void
  pack(const Options::Pack &options_)
  {
    fs::path output_path;
    fs::path temp_output_path;
    TDO::DiscManifest manifest;

    try
      {
        manifest = create_manifest(options_);
        preflight_input_files(manifest.root);
      }
    catch(const std::exception &e)
      {
        throw Error(e.what());
      }

    if(options_.dry_run)
      {
        fmt::print("{}:\n"
                   "  - dry run: true\n"
                   "  - total blocks: {}\n"
                   "  - total bytes: {}\n",
                   options_.output,
                   manifest.total_blocks,
                   static_cast<u64>(manifest.total_blocks) * TDO::BLOCK_SIZE);
        return;
      }

    output_path = manifest.output;
    try
      {
        temp_output_path = temp_path_for(output_path);
        manifest.output = temp_output_path;
      }
    catch(const std::exception &e)
      {
        throw Error(e.what());
      }

    try
      {
        TDO::pack_disc_image(manifest);
        list_packed_image(temp_output_path);

        const bool recreate_layout_specials = (manifest.replay_layout &&
                                               options_.sign);
        const auto digest_check_count =
          static_cast<std::uint8_t>(options_.signature_digest_check_count);

        if(options_.mark)
          {
            TDO::mark_disc_image(temp_output_path,
                                 (options_.sign ?
                                  "packed and signed" :
                                  "packed"));
          }

        if(recreate_layout_specials)
          {
            TDO::recreate_layout_special_files(temp_output_path,
                                               options_.sign,
                                               false,
                                               options_.banner_romtag,
                                               options_.billstuff_romtag,
                                               digest_check_count);
          }

        if(options_.sign && !recreate_layout_specials)
          {
            TDO::sign_disc_image(temp_output_path,
                                 false,
                                 true,
                                 options_.banner_romtag,
                                 options_.billstuff_romtag,
                                 digest_check_count);
          }

        if(options_.sign)
          {
            Options::Verify verify_opts{};

            fmt::print("{}:\n  - Verifying signed image\n",temp_output_path);
            verify_opts.filepaths.emplace_back(temp_output_path);
            Subcmd::verify(verify_opts);
          }
        else
          {
            fmt::print("{}:\n  - Verifying OperaFS structure\n",temp_output_path);
            verify_operafs_structure_file(temp_output_path);
          }

        fs::rename(temp_output_path,output_path);
      }
    catch(const std::exception &e)
      {
        std::error_code ec;

        fs::remove(temp_output_path,ec);
        throw;
      }
  }
}
