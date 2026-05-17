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

#include "tdo_disc_signer.hpp"

#include "md5.h"
#include "fmt.hpp"
#include "fmt_md5_digest.hpp"
#include "fmt_rsa512_sig.hpp"
#include "nonstd/string.hpp"
#include "tdo_boot_code_crypto.hpp"
#include "tdo_disc_format.hpp"
#include "tdo_file_stream.hpp"
#include "tdo_fs_walker.hpp"
#include "tdo_rsa.h"
#include "version.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace
{
  static constexpr u32 ARM_NOP = 0xe1a00000;
  static constexpr u32 AIF_EXIT_INSTRUCTION = 0xef000011;
  static constexpr u32 AIF_RELOCATION_LIST_END = 0xffffffff;
  static constexpr u64 AIF_HEADER_RO_SIZE_OFFSET = 0x14;
  static constexpr u64 AIF_HEADER_RW_SIZE_OFFSET = 0x18;
  static constexpr u64 AIF_HEADER_DEBUG_SIZE_OFFSET = 0x1c;

  static
  u32
  checked_u32(const u64   value_,
              const char *label_)
  {
    if(value_ > std::numeric_limits<u32>::max())
      throw Error(std::string(label_) + " is too large");

    return static_cast<u32>(value_);
  }

  static
  u32
  read_u32_be(const std::vector<char> &data_,
              const u64                offset_)
  {
    return ((static_cast<u32>(static_cast<u8>(data_[offset_ + 0])) << 24) |
            (static_cast<u32>(static_cast<u8>(data_[offset_ + 1])) << 16) |
            (static_cast<u32>(static_cast<u8>(data_[offset_ + 2])) << 8) |
            (static_cast<u32>(static_cast<u8>(data_[offset_ + 3])) << 0));
  }

  static
  std::optional<u64>
  arm_bl_target(const u32 instruction_,
                const u64 instruction_offset_)
  {
    s32 immediate;
    s64 target;

    if((instruction_ & 0x0f000000) != 0x0b000000)
      return {};

    immediate = (instruction_ & 0x00ffffff);
    if((immediate & 0x00800000) != 0)
      immediate |= 0xff000000;

    target = static_cast<s64>(instruction_offset_) + 8 +
             (static_cast<s64>(immediate) << 2);
    if(target < 0)
      return {};

    return static_cast<u64>(target);
  }

  static
  bool
  is_zero_range(const std::vector<char> &data_,
                const u64                begin_,
                const u64                end_)
  {
    for(u64 pos = begin_; pos < end_; pos++)
      if(data_[pos] != 0)
        return false;

    return true;
  }

  static
  std::optional<u64>
  aif_executable_size(const std::vector<char> &data_)
  {
    u64 image_size;
    u64 ro_size;
    u64 rw_size;
    u64 debug_size;
    u64 executable_size;
    u32 decompress_instruction;
    u32 self_reloc_instruction;
    u32 zero_init_instruction;
    u32 entry_instruction;
    std::optional<u64> self_reloc_target;

    if(data_.size() < 0x40)
      return {};

    decompress_instruction = read_u32_be(data_,0x00);
    self_reloc_instruction = read_u32_be(data_,0x04);
    zero_init_instruction = read_u32_be(data_,0x08);
    entry_instruction = read_u32_be(data_,0x0c);

    if(read_u32_be(data_,0x10) != AIF_EXIT_INSTRUCTION)
      return {};
    if((decompress_instruction != ARM_NOP) &&
       !arm_bl_target(decompress_instruction,0x00))
      return {};
    self_reloc_target = arm_bl_target(self_reloc_instruction,0x04);
    if((self_reloc_instruction != ARM_NOP) && !self_reloc_target)
      return {};
    if((zero_init_instruction != ARM_NOP) &&
       !arm_bl_target(zero_init_instruction,0x08))
      return {};
    if(!arm_bl_target(entry_instruction,0x0c))
      return {};

    ro_size = read_u32_be(data_,AIF_HEADER_RO_SIZE_OFFSET);
    rw_size = read_u32_be(data_,AIF_HEADER_RW_SIZE_OFFSET);
    debug_size = read_u32_be(data_,AIF_HEADER_DEBUG_SIZE_OFFSET);

    image_size = ro_size + rw_size + debug_size;
    if((image_size < 0x40) ||
       (image_size < ro_size) ||
       (image_size < rw_size) ||
       (image_size > data_.size()))
      return {};

    executable_size = image_size;
    if(self_reloc_target &&
       ((*self_reloc_target % 4) == 0) &&
       (*self_reloc_target >= executable_size))
      {
        for(u64 pos = *self_reloc_target;
            (pos + sizeof(u32)) <= data_.size();
            pos += sizeof(u32))
          {
            if(read_u32_be(data_,pos) != AIF_RELOCATION_LIST_END)
              continue;

            executable_size = pos + sizeof(u32);
            break;
          }
      }

    // Some unsigned boot_code files keep one zero word after the AIF
    // relocation terminator before the signature slots.
    if(((executable_size + sizeof(u32)) <= data_.size()) &&
       (read_u32_be(data_,executable_size) == 0) &&
       is_zero_range(data_,executable_size,data_.size()))
      executable_size += sizeof(u32);

    return executable_size;
  }

  static
  std::optional<u64>
  boot_code_romtag_size(const std::vector<char> &encrypted_data_)
  {
    u64 signed_size;
    u64 aligned_size;
    std::vector<char> data;
    std::optional<u64> executable_size;

    data = encrypted_data_;
    aligned_size = TDO::boot_code_crypto_aligned_size(data.size());
    TDO::decrypt_boot_code_range(data.data(),aligned_size);

    executable_size = aif_executable_size(data);
    if(!executable_size)
      return {};

    signed_size = TDO::round_up(*executable_size,sizeof(u32)) + (RSA512_SIG_SIZE * 2);
    if(signed_size > data.size())
      return {};
    if((signed_size < data.size()) &&
       !is_zero_range(data,signed_size,data.size()))
      return {};

    return signed_size;
  }

  static
  void
  apply_os_romtag_version(TDO::DevStream        &stream_,
                          TDO::ROMTag          &romtag_,
                          const TDO::DirectoryRecord &record_)
  {
    static constexpr u64 COMPONENT_VERSION_OFFSET = 0xA4;
    std::array<char, COMPONENT_VERSION_OFFSET + 2> buf{};

    if(record_.byte_count < (COMPONENT_VERSION_OFFSET + 2))
      return;

    stream_.read_data_bytes_from_block(buf.data(),
                                       record_.avatar_list[0],
                                       COMPONENT_VERSION_OFFSET + 2);
    romtag_.version = static_cast<u8>(buf[COMPONENT_VERSION_OFFSET]);
    romtag_.revision = static_cast<u8>(buf[COMPONENT_VERSION_OFFSET + 1]);
  }

  static
  void
  apply_boot_romtag_version(TDO::ROMTag &romtag_,
                            const u64    record_size_)
  {
    romtag_.version = ((record_size_ < 8192) ? 1 : 2);
    romtag_.revision = 0;
  }

  static
  void
  require_iso2048_image(TDO::FileStream &stream_)
  {
    if((stream_.device_block_header() != 0) ||
       (stream_.device_block_footer() != 0) ||
       (stream_.device_block_data_size() != TDO::BLOCK_SIZE))
      throw Error("signing is currently supported only for 2048-byte ISO images");
  }

  static
  void
  update_record_sizes(TDO::DevStream &stream_,
                      const u32       record_pos_,
                      const u32       byte_count_,
                      const u32       block_count_)
  {
    stream_.file_seek(record_pos_);
    stream_.data_byte_skip(offsetof(TDO::DirectoryRecord,byte_count));
    stream_.write(byte_count_);
    stream_.write(block_count_);
  }

  class ROMTagsFileUpdater final : public TDO::FSWalker::Callbacks
  {
  public:
    u32 romtags_file_size;

  public:
    void
    operator()(const std::filesystem::path &filepath_,
               const TDO::DirectoryRecord  &record_,
               const u32                    record_pos_,
               TDO::DevStream              &stream_)
    {
      (void)record_;

      if(nonstd::string::as_lowercase(filepath_.string()) != "rom_tags")
        return;

      update_record_sizes(stream_,
                          record_pos_,
                          romtags_file_size,
                          TDO::div_round_up(romtags_file_size,TDO::BLOCK_SIZE));
    }
  };

  class SignaturesFileUpdater final : public TDO::FSWalker::Callbacks
  {
  public:
    u32 signatures_block_count;
    u32 signatures_record_size;

  public:
    void
    operator()(const std::filesystem::path &filepath_,
               const TDO::DirectoryRecord  &record_,
               const u32                    record_pos_,
               TDO::DevStream              &stream_)
    {
      (void)record_;

      if(nonstd::string::as_lowercase(filepath_.string()) != "signatures")
        return;

      update_record_sizes(stream_,
                          record_pos_,
                          signatures_record_size,
                          std::max<u32>(record_.block_count,
                                        signatures_block_count));
    }
  };

  static
  u32
  romtag_type_for_path(const std::string &lc_path_,
                       const bool         include_banner_)
  {
    if(lc_path_ == "signatures")
      return RSA_SIGNATURE_BLOCK;
    if(lc_path_ == "system/kernel/boot_code")
      return RSA_NEWKNEWNEWGNUBOOT;
    if(lc_path_ == "system/kernel/misc_code")
      return RSA_MISCCODE;
    if(lc_path_ == "system/kernel/os_code")
      return RSA_OS;
    if(lc_path_ == "launchme")
      return RSA_BLOCKS_ALWAYS;
    if(include_banner_ && (lc_path_ == "bannerscreen"))
      return RSA_APPSPLASH;
    return 0;
  }

  class SignaturesFileCapacity final : public TDO::FSWalker::Callbacks
  {
  public:
    bool found;
    u64  byte_count;

  public:
    SignaturesFileCapacity()
      : found(false),
        byte_count(0)
    {
    }

  public:
    void
    operator()(const std::filesystem::path &filepath_,
               const TDO::DirectoryRecord  &record_,
               const u32                    record_pos_,
               TDO::DevStream              &stream_)
    {
      (void)record_pos_;
      (void)stream_;

      if(nonstd::string::as_lowercase(filepath_.string()) != "signatures")
        return;

      found = true;
      byte_count = static_cast<u64>(record_.block_count) * record_.block_size;
    }
  };

  class SpecialFileCapacity final : public TDO::FSWalker::Callbacks
  {
  public:
    bool found_rom_tags;
    bool found_signatures;
    u64  rom_tags_capacity;
    u64  signatures_capacity;
    u32  signatures_record_pos;
    u32  signatures_record_size;
    u32  signatures_block_count;

  public:
    SpecialFileCapacity()
      : found_rom_tags(false),
        found_signatures(false),
        rom_tags_capacity(0),
        signatures_capacity(0),
        signatures_record_pos(0),
        signatures_record_size(0),
        signatures_block_count(0)
    {
    }

  public:
    void
    operator()(const std::filesystem::path &filepath_,
               const TDO::DirectoryRecord  &record_,
               const u32                    record_pos_,
               TDO::DevStream              &stream_)
    {
      std::string lc_filepath;

      (void)record_pos_;
      (void)stream_;

      lc_filepath = nonstd::string::as_lowercase(filepath_.string());
      if(lc_filepath == "rom_tags")
        {
          found_rom_tags = true;
          rom_tags_capacity = static_cast<u64>(record_.block_count) * record_.block_size;
        }
      else if(lc_filepath == "signatures")
        {
          found_signatures = true;
          signatures_capacity = static_cast<u64>(record_.block_count) * record_.block_size;
          signatures_record_pos = record_pos_;
          signatures_record_size = record_.byte_count;
          signatures_block_count = record_.block_count;
        }
    }
  };

  class SigningPreflight final : public TDO::FSWalker::Callbacks
  {
  public:
    bool found_boot_code;
    bool found_misc_code;
    bool found_os_code;
    bool found_rom_tags;
    bool found_signatures;
    u32  generated_romtag_count;
    u64  rom_tags_capacity;
    u64  signatures_capacity;
    bool include_banner_romtag;

  public:
    SigningPreflight(const bool include_banner_romtag_)
      : found_boot_code(false),
        found_misc_code(false),
        found_os_code(false),
        found_rom_tags(false),
        found_signatures(false),
        generated_romtag_count(0),
        rom_tags_capacity(0),
        signatures_capacity(0),
        include_banner_romtag(include_banner_romtag_)
    {
    }

  public:
    void
    operator()(const std::filesystem::path &filepath_,
               const TDO::DirectoryRecord  &record_,
               const u32                    record_pos_,
               TDO::DevStream              &stream_)
    {
      u32 type;
      std::string lc_filepath;

      (void)record_pos_;
      (void)stream_;

      lc_filepath = nonstd::string::as_lowercase(filepath_.string());
      if(lc_filepath == "rom_tags")
        {
          found_rom_tags = true;
          rom_tags_capacity = static_cast<u64>(record_.block_count) * record_.block_size;
        }
      else if(lc_filepath == "signatures")
        {
          found_signatures = true;
          signatures_capacity = static_cast<u64>(record_.block_count) * record_.block_size;
        }
      else if(lc_filepath == "system/kernel/boot_code")
        found_boot_code = true;
      else if(lc_filepath == "system/kernel/misc_code")
        found_misc_code = true;
      else if(lc_filepath == "system/kernel/os_code")
        found_os_code = true;

      type = romtag_type_for_path(lc_filepath, include_banner_romtag);
      if(type != 0)
        generated_romtag_count++;
    }
  };

  class ROMTagsGenerator final : public TDO::FSWalker::Callbacks
  {
  public:
    TDO::ROMTagVec romtags;
    bool           include_banner_romtag;
    std::uint8_t   digest_check_count;

  public:
    ROMTagsGenerator(const bool         include_banner_romtag_,
                     const std::uint8_t digest_check_count_)
      : romtags(),
        include_banner_romtag(include_banner_romtag_),
        digest_check_count(digest_check_count_)
    {
    }

  public:
    void
    operator()(const std::filesystem::path &filepath_,
               const TDO::DirectoryRecord  &record_,
               const u32                    record_pos_,
               TDO::DevStream              &stream_)
    {
      u32 type;

      type = romtag_type_for_path(nonstd::string::as_lowercase(filepath_.string()),
                                  include_banner_romtag);
      if(type == 0)
        return;

      TDO::ROMTag romtag{};

      romtag.type        = type;
      romtag.sub_systype = RSANODE;
      romtag.size        = record_.byte_count;
      romtag.offset      = record_.avatar_list[0] - 1;
      switch(type)
        {
        case RSA_SIGNATURE_BLOCK:
          romtag.type_specific = digest_check_count;
          break;
        case RSA_BLOCKS_ALWAYS:
          romtag.offset = record_.avatar_list[0];
          romtag.size = record_.block_count;
          break;
        case RSA_OS:
          apply_os_romtag_version(stream_,romtag,record_);
          break;
        case RSA_NEWKNEWNEWGNUBOOT:
          {
            u64 allocated_size;
            std::vector<char> buf;
            std::optional<u64> boot_size;

            allocated_size = static_cast<u64>(record_.block_count) * record_.block_size;
            stream_.read_data_bytes_from_block(buf,
                                               record_.avatar_list[0],
                                               allocated_size);

            boot_size = boot_code_romtag_size(buf);
            if(!boot_size)
              boot_size = TDO::round_up(record_.byte_count,sizeof(u32));
            apply_boot_romtag_version(romtag,
                                      record_.byte_count);
            if(boot_size && (*boot_size != record_.byte_count))
              {
                fmt::print("    - correcting boot_code size to {}\n",
                           *boot_size);
                romtag.size = *boot_size;
                update_record_sizes(stream_,
                                     record_pos_,
                                     romtag.size,
                                    record_.block_count);
              }
          }
          break;
        }

      romtags.emplace_back(romtag);
    }
  };

  static
  u32
  romtag_sort_key(const TDO::ROMTag &tag_)
  {
    switch(tag_.type)
      {
      case RSA_NEWKNEWNEWGNUBOOT:
        return 0;
      case RSA_OS:
        return 1;
      case RSA_BILLSTUFF:
        return 2;
      case RSA_BLOCKS_ALWAYS:
        return 3;
      case RSA_MISCCODE:
        return 4;
      case RSA_APPSPLASH:
        return 5;
      case RSA_SIGNATURE_BLOCK:
        return 6;
      }

    return 100;
  }

  static
  void
  pad_image_and_update_disclabel(TDO::FileStream &stream_)
  {
    TDO::DiscLabel dl;

    fmt::print("  - Pad image and update disc label\n"
               "    - original size: {}b\n",
               stream_.size_in_bytes());
    stream_.resize_multiple(TDO::LOG_BLOCK_SIZE);
    fmt::print("    - padded size:  {}b\n",
               stream_.size_in_bytes());

    dl = stream_.disc_label();
    dl.volume_block_count = stream_.size_in_device_blocks();

    stream_.data_block_seek(stream_.disc_label_block());
    stream_.write(dl);
  }

  static
  void
  add_3dt_mark(TDO::FileStream &stream_,
               const std::string &action_)
  {
    std::string mark;
    const u64 mark_offset = 0x100;

    mark = fmt::format("{} with 3dt v{}.{}.{}",
                       action_,
                       VERSION_MAJOR,
                       VERSION_MINOR,
                       VERSION_PATCH);
     mark.resize(64,'\0');

     fmt::print("  - Setting location {} to '{}'\n",
               mark_offset,
               mark.c_str());
    stream_.data_byte_seek(mark_offset);
    stream_.write(mark.c_str(),mark.size());
  }

  static
  void
  write_romtags(TDO::FileStream      &stream_,
                const TDO::ROMTagVec &romtags_)
  {
    stream_.data_block_seek(stream_.romtags_block());
    for(auto &tag : romtags_)
      {
        fmt::print("    - type: {}; offset: {}; size: {}b\n",
                   TDO::ROMTag::type_str(tag.type),
                   tag.offset + 1,
                   tag.size);
        stream_.write(tag);
      }
    stream_.write(TDO::ROMTag{});
  }

  static
  std::optional<TDO::ROMTag>
  find_romtag(const TDO::ROMTagVec &romtags_,
              const u8              type_)
  {
    for(const auto &romtag : romtags_)
      if(romtag.type == type_)
        return romtag;

    return {};
  }

  static
  void
  add_billstuff_romtag(TDO::FileStream &stream_,
                       TDO::ROMTagVec  &romtags_)
  {
    TDO::ROMTag billstuff{};

    billstuff.sub_systype = RSANODE;
    billstuff.type        = RSA_BILLSTUFF;
    billstuff.offset      = (stream_.disc_label().volume_unique_identifier ^
                             stream_.disc_label().root_unique_identifier);

    romtags_.emplace_back(billstuff);
  }

  static
  void
  sort_romtags(TDO::ROMTagVec &romtags_)
  {
    std::stable_sort(romtags_.begin(),
                     romtags_.end(),
                     [](const TDO::ROMTag &lhs_,
                        const TDO::ROMTag &rhs_)
                     {
                       return (romtag_sort_key(lhs_) < romtag_sort_key(rhs_));
                     });
  }

  static
  TDO::ROMTagVec
  generate_romtags_for_image(TDO::FileStream &stream_,
                             const bool       include_banner_romtag_,
                             const std::uint8_t digest_check_count_)
  {
    ROMTagsGenerator tags(include_banner_romtag_,digest_check_count_);
    TDO::FSWalker fswalker(stream_,tags,false);

    fswalker.walk();

    add_billstuff_romtag(stream_,tags.romtags);
    sort_romtags(tags.romtags);

    return tags.romtags;
  }

  static
  void
  preflight_layout_special_files(TDO::FileStream           &stream_,
                                 const TDO::ROMTagVec      &romtags_,
                                 const SpecialFileCapacity &capacity_)
  {
    if(!capacity_.found_rom_tags)
      throw Error("image is missing file: rom_tags");

    const u64 romtags_size =
      ((static_cast<u64>(romtags_.size()) + 1) * sizeof(TDO::ROMTag)) + RSA512_SIG_SIZE;
    if(romtags_size > capacity_.rom_tags_capacity)
      throw Error("rom_tags file too small, increase size and rebuild image");

    const auto sig_romtag = find_romtag(romtags_,RSA_SIGNATURE_BLOCK);
    if(!sig_romtag)
      return;

    if(!capacity_.found_signatures)
      throw Error("image is missing file: signatures");

    const u64 volume_block_count = stream_.disc_label().volume_block_count;
    const u64 num_digests = ((volume_block_count * TDO::BLOCK_SIZE) / TDO::LOG_BLOCK_SIZE);
    const u64 file_size = TDO::signature_file_size_for_digest_count(num_digests);
    const u64 record_size = TDO::signature_record_size_for_digest_count(num_digests);
    if(file_size > capacity_.signatures_capacity)
      throw Error("signatures file too small, increase size and rebuild image");
    if(sig_romtag->size < record_size)
      throw Error("signatures file too small, increase size and rebuild image");
  }

  static
  void
  update_layout_signatures_record(TDO::FileStream           &stream_,
                                  const SpecialFileCapacity &capacity_)
  {
    if(!capacity_.found_signatures)
      return;

    const u64 volume_block_count = stream_.disc_label().volume_block_count;
    const u64 num_digests = ((volume_block_count * TDO::BLOCK_SIZE) / TDO::LOG_BLOCK_SIZE);
    const u64 file_size = TDO::signature_file_size_for_digest_count(num_digests);
    const u64 record_size =
      std::max<u64>(capacity_.signatures_record_size,
                    TDO::signature_record_size_for_digest_count(num_digests));

    if(file_size > capacity_.signatures_capacity)
      throw Error("signatures file too small, increase size and rebuild image");

    update_record_sizes(stream_,
                        capacity_.signatures_record_pos,
                        record_size,
                        capacity_.signatures_block_count);
  }

  static
  void
  update_romtags_file(TDO::FileStream &stream_,
                      const u32        size_)
  {
    ROMTagsFileUpdater updater;
    TDO::FSWalker fsw(stream_,updater);

    updater.romtags_file_size = size_;
    fsw.walk();
  }

  static
  void
  preflight_signing_image(TDO::FileStream &stream_,
                          const bool       include_banner_romtag_)
  {
    SigningPreflight preflight(include_banner_romtag_);
    TDO::FSWalker fsw(stream_,preflight);

    if(!stream_.has_romtags())
      throw Error("image does not contain ROMTags");

    fsw.walk();

    if(!preflight.found_rom_tags)
      throw Error("image is missing file: rom_tags");
    if(!preflight.found_signatures)
      throw Error("image is missing file: signatures");
    if(!preflight.found_boot_code)
      throw Error("image is missing file: system/kernel/boot_code");
    if(!preflight.found_os_code)
      throw Error("image is missing file: system/kernel/os_code");
    if(!preflight.found_misc_code)
      throw Error("image is missing file: system/kernel/misc_code");

    const u64 padded_size = TDO::round_up(stream_.size_in_device_blocks() * TDO::BLOCK_SIZE,
                                     TDO::LOG_BLOCK_SIZE);
    const u64 volume_block_count = padded_size / TDO::BLOCK_SIZE;
    const u64 num_digests = ((volume_block_count * TDO::BLOCK_SIZE) / TDO::LOG_BLOCK_SIZE);
    const u64 signature_size = TDO::signature_file_size_for_digest_count(num_digests);
    if(signature_size > preflight.signatures_capacity)
      throw Error("signatures file too small, increase size and rebuild image");

    // Account for generated ROMTags, the terminating ROMTag, and the cross-app
    // signature stored immediately after the terminator.
    const u64 romtags_size = ((preflight.generated_romtag_count + 2) * sizeof(TDO::ROMTag));
    if((romtags_size + RSA512_SIG_SIZE) > preflight.rom_tags_capacity)
      throw Error("rom_tags file too small, increase size and rebuild image");
  }

  static
  void
  generate_and_write_romtags(TDO::FileStream &stream_,
                             const bool       include_banner_romtag_,
                             const std::uint8_t digest_check_count_)
  {
    TDO::ROMTagVec romtags;

    fmt::print("  - Generate and write ROM Tags\n");
    romtags = generate_romtags_for_image(stream_,
                                         include_banner_romtag_,
                                         digest_check_count_);
    write_romtags(stream_,romtags);
    update_romtags_file(stream_,romtags.size() * sizeof(TDO::ROMTag));
  }

  static
  void
  sign_romtag_payload(TDO::FileStream &stream_,
                      const u8         romtag_type_,
                      const char      *key_,
                      const char      *label_)
  {
    md5_digest_t digest;
    rsa512_sig_t sig;
    std::vector<char> data;
    std::optional<TDO::ROMTag> romtag;

    romtag = stream_.romtag(romtag_type_);
    if(!romtag)
      return;
    if(romtag->size < RSA512_SIG_SIZE)
      throw Error(std::string(label_) + " is too small to contain a signature");

    stream_.read_data_bytes_from_block(data,
                                       romtag->offset + 1,
                                       romtag->size - RSA512_SIG_SIZE);
    md5_calc(data.data(),data.size(),digest);
    tdo_rsa_sign(key_,digest,sig);

    fmt::print("  - Signing {}\n"
               "    - MD5 digest: {}\n"
               "    - RSA signature: {}\n",
               label_,
               digest,
               sig);

    stream_.data_byte_seek(((romtag->offset + 1) * TDO::BLOCK_SIZE) +
                           (romtag->size - RSA512_SIG_SIZE));
    stream_.write((const char*)sig,sizeof(sig));
  }

  static
  void
  sign_appsplash(TDO::FileStream &stream_)
  {
    sign_romtag_payload(stream_,RSA_APPSPLASH,TDO_KEY_APP,"BannerScreen");
  }

  static
  void
  sign_boot_code(TDO::FileStream &stream_)
  {
    md5_digest_t digest;
    rsa512_sig_t sig;
    std::vector<char> data;
    std::array<char, RSA512_SIG_SIZE> encrypted_sig;
    std::optional<TDO::ROMTag> romtag;

    romtag = stream_.romtag(RSA_NEWKNEWNEWGNUBOOT);
    if(!romtag)
      return;
    if(romtag->size < (RSA512_SIG_SIZE * 2))
      throw Error("boot_code is too small to contain both signatures");

    const u64 post_cheeze_sig_offset = romtag->size - (RSA512_SIG_SIZE * 2);
    const u64 outer_sig_offset = romtag->size - RSA512_SIG_SIZE;
    stream_.read_data_bytes_from_block(data,
                                       romtag->offset + 1,
                                       romtag->size);

    TDO::decrypt_boot_code_range(data.data(),post_cheeze_sig_offset);
    md5_calc(data.data(),post_cheeze_sig_offset,digest);
    tdo_rsa_sign(TDO_KEY_3DO,digest,sig);

    fmt::print("  - Signing decrypted boot_code\n"
               "    - MD5 digest: {}\n"
               "    - RSA signature: {}\n",
               digest,
               sig);

    std::memcpy(encrypted_sig.data(),sig,sizeof(sig));
    TDO::encrypt_boot_code_range(encrypted_sig.data(),
                                 encrypted_sig.size(),
                                 post_cheeze_sig_offset);

    stream_.data_byte_seek(((romtag->offset + 1) * TDO::BLOCK_SIZE) +
                           post_cheeze_sig_offset);
    stream_.write(encrypted_sig.data(),encrypted_sig.size());

    data.clear();
    stream_.read_data_bytes_from_block(data,
                                       romtag->offset + 1,
                                       outer_sig_offset);
    md5_calc(data.data(),data.size(),digest);
    tdo_rsa_sign(TDO_KEY_3DO,digest,sig);

    fmt::print("  - Signing encrypted boot_code\n"
               "    - MD5 digest: {}\n"
               "    - RSA signature: {}\n",
               digest,
               sig);

    stream_.data_byte_seek(((romtag->offset + 1) * TDO::BLOCK_SIZE) +
                           outer_sig_offset);
    stream_.write((const char*)sig,sizeof(sig));
  }

  static
  void
  sign_system_payloads(TDO::FileStream &stream_)
  {
    sign_boot_code(stream_);
    sign_romtag_payload(stream_,RSA_OS,TDO_KEY_3DO,"os_code");
    sign_romtag_payload(stream_,RSA_MISCCODE,TDO_KEY_3DO,"misc_code");
  }

  static
  std::vector<char>
  generate_signatures_file_data(TDO::FileStream &stream_)
  {
    u64 num_digests;
    u64 volume_block_count;
    u64 volume_block_size;
    md5_digest_t digest;
    std::vector<char> buf;
    std::vector<char> signatures;

    volume_block_count = stream_.disc_label().volume_block_count;
    volume_block_size  = stream_.disc_label().volume_block_size;
    num_digests        = ((volume_block_count * volume_block_size) / TDO::LOG_BLOCK_SIZE);

    signatures.resize(num_digests * sizeof(digest));

    fmt::print("  - Generate and sign signatures file with APP key\n"
               "    - block count: {}\n"
               "    - num digests: {}\n",
               volume_block_count,
               num_digests);
    for(u64 i = 0; i < num_digests; i++)
      {
        s64 block_pos;

        block_pos = ((i * TDO::LOG_BLOCK_SIZE) / TDO::BLOCK_SIZE);

        buf.clear();
        stream_.read_data_blocks(buf,block_pos,(TDO::LOG_BLOCK_SIZE / TDO::BLOCK_SIZE));

        md5_calc(buf.data(),buf.size(),digest);

        std::memcpy(&signatures[i * sizeof(digest)],digest,sizeof(digest));
      }

    return signatures;
  }

  static
  void
  update_signature_romtag_size(TDO::FileStream &stream_,
                               const u32        signatures_record_size_)
  {
    bool found;

    found = false;
    stream_.data_block_seek(stream_.romtags_block());
    while(true)
      {
        u64 offset;
        TDO::ROMTag romtag;

        offset = stream_.file_tell();
        stream_.read(romtag);
        if((romtag.sub_systype == 0) || (romtag.type == 0))
          break;
        if(romtag.type != RSA_SIGNATURE_BLOCK)
          continue;

        stream_.file_seek(offset);
        stream_.data_byte_skip(offsetof(TDO::ROMTag,size));
        stream_.write(signatures_record_size_);
        found = true;
        break;
      }

    if(!found)
      throw Error("signatures ROM tag not found");
  }

  static
  u64
  signature_file_capacity(TDO::FileStream &stream_)
  {
    SignaturesFileCapacity capacity;
    TDO::FSWalker fsw(stream_,capacity);

    fsw.walk();
    if(!capacity.found)
      throw Error("signatures file not found");

    return capacity.byte_count;
  }

  static
  void
  zero_signature_digests(std::vector<char> &signatures_,
                         const u64          start_block_,
                         const u64          byte_count_)
  {
    if(byte_count_ == 0)
      return;

    const u64 start_byte = start_block_ * TDO::BLOCK_SIZE;
    const u64 end_byte = start_byte + byte_count_;
    const u64 first_digest = start_byte / TDO::LOG_BLOCK_SIZE;
    const u64 last_digest = (end_byte - 1) / TDO::LOG_BLOCK_SIZE;

    for(u64 digest = first_digest; digest <= last_digest; digest++)
      {
        const u64 offset = digest * sizeof(md5_digest_t);

        if((offset + sizeof(md5_digest_t)) > signatures_.size())
          break;
        std::fill(signatures_.begin() + offset,
                  signatures_.begin() + offset + sizeof(md5_digest_t),
                  0);
      }
  }

  static
  void
  zero_mutable_signature_digests(TDO::FileStream &stream_,
                                 std::vector<char> &signatures_)
  {
    std::optional<TDO::ROMTag> sig_romtag;

    sig_romtag = stream_.romtag(RSA_SIGNATURE_BLOCK);
    if(sig_romtag)
      zero_signature_digests(signatures_,sig_romtag->offset + 1,sig_romtag->size);
  }

  static
  void
  recreate_layout_signatures_file(TDO::FileStream &stream_)
  {
    md5_digest_t digest;
    rsa512_sig_t sig;
    u64 file_size;
    u64 num_digests;
    std::optional<TDO::ROMTag> sig_romtag;
    std::vector<char> signatures;

    sig_romtag = stream_.romtag(RSA_SIGNATURE_BLOCK);
    if(!sig_romtag)
      return;

    signatures = generate_signatures_file_data(stream_);
    zero_mutable_signature_digests(stream_,signatures);

    num_digests = ((stream_.disc_label().volume_block_count * TDO::BLOCK_SIZE) / TDO::LOG_BLOCK_SIZE);
    file_size = std::max<u64>(TDO::signature_file_size_for_digest_count(num_digests),
                              sig_romtag->size);
    if(file_size > signature_file_capacity(stream_))
      throw Error("signatures file too small, increase size and rebuild image");
    if((signatures.size() + sizeof(sig)) > file_size)
      throw Error("signatures file too small, increase size and rebuild image");

    signatures.resize(file_size);

    md5_calc(signatures.data(),file_size - sizeof(sig),digest);
    tdo_rsa_sign(TDO_KEY_APP,digest,sig);

    fmt::print("    - signatures size: {}b\n"
               "    - MD5 digest: {}\n"
               "    - RSA signature: {}\n",
               file_size,
               digest,
               sig);

    std::memcpy(&signatures[file_size - sizeof(sig)],sig,sizeof(sig));

    const s64 sig_offset = sig_romtag->offset + 1;
    for(u64 i = 0; i < TDO::div_round_up(signatures.size(),TDO::BLOCK_SIZE); i++)
      {
        const u64 offset = i * TDO::BLOCK_SIZE;
        const u64 bytes_left = signatures.size() - offset;
        const u64 bytes_to_write = std::min<u64>(TDO::BLOCK_SIZE,bytes_left);

        stream_.data_block_seek(sig_offset + i);
        stream_.write(&signatures[offset],bytes_to_write);
      }
  }

  static
  void
  resize_signatures_file_record(TDO::FileStream &stream_)
  {
    u64 file_size;
    u64 num_digests;
    u64 record_size;

    std::optional<TDO::ROMTag> sig_romtag;
    sig_romtag = stream_.romtag(RSA_SIGNATURE_BLOCK);
    if(!sig_romtag)
      throw Error("signatures ROM tag not found");

    // Keep the logical record size separate from the physical file capacity;
    // appdigest.c intentionally makes them differ at 512-digest boundaries.
    num_digests = ((stream_.disc_label().volume_block_count * TDO::BLOCK_SIZE) / TDO::LOG_BLOCK_SIZE);
    file_size = TDO::signature_file_size_for_digest_count(num_digests);
    record_size = std::max<u64>(sig_romtag->size,
                                TDO::signature_record_size_for_digest_count(num_digests));
    file_size = std::max(file_size,record_size);
    if(file_size > signature_file_capacity(stream_))
      throw Error("signatures file too small, increase size and rebuild image");

    update_signature_romtag_size(stream_,checked_u32(record_size,
                                                     "signatures record size"));

    SignaturesFileUpdater sfu;
    TDO::FSWalker fsw(stream_,sfu);

    sfu.signatures_block_count = TDO::div_round_up(file_size,TDO::BLOCK_SIZE);
    sfu.signatures_record_size = checked_u32(record_size,
                                             "signatures record size");
    fsw.walk();
  }

  static
  void
  sign_disclabel_romtags_bootcode(TDO::FileStream &stream_)
  {
    md5_digest_t digest;
    rsa512_sig_t signature;
    std::vector<char> data;
    std::optional<TDO::ROMTag> romtag;

    romtag = stream_.romtag(RSA_NEWKNEWNEWGNUBOOT);
    if(!romtag)
      throw Error("boot_code ROM tag not found");

    stream_.read_data_bytes_from_block(data,
                                       stream_.disc_label_block(),
                                       stream_.disc_label_size_in_bytes());
    stream_.read_data_bytes_from_block(data,
                                       stream_.romtags_block(),
                                       stream_.romtags_size_in_bytes());
    stream_.read_data_bytes_from_block(data,
                                       romtag->offset + 1,
                                       romtag->size);

    md5_calc(data.data(),data.size(),digest);
    tdo_rsa_sign(TDO_KEY_APP,digest,signature);

    fmt::print("  - Signing DiscLabel + ROMTags + BootCode with APP key\n"
               "    - MD5 digest: {}\n"
               "    - RSA signature: {}\n",
               digest,
               signature);

    stream_.data_block_seek(stream_.romtags_block());
    stream_.data_byte_skip(stream_.romtags_size_in_bytes());
    stream_.write((char*)signature,sizeof(signature));
  }
}

void
TDO::recreate_layout_special_files(const std::filesystem::path &filepath_,
                                   const bool                   sign_payloads_,
                                   const bool                   mark_,
                                   const bool                   include_banner_romtag_,
                                   const std::uint8_t           digest_check_count_)
{
  SpecialFileCapacity capacity;
  TDO::ROMTagVec romtags;
  TDO::FileStream stream;

  stream.open(filepath_,std::ios::in|std::ios::out);
  require_iso2048_image(stream);

  fmt::print("{}:\n",filepath_);
  fmt::print("  - Recreate layout special files\n");

  {
    TDO::FSWalker fsw(stream,capacity,false);

    fsw.walk();
  }
  pad_image_and_update_disclabel(stream);
  update_layout_signatures_record(stream,capacity);
  romtags = generate_romtags_for_image(stream,
                                       include_banner_romtag_,
                                       digest_check_count_);
  preflight_layout_special_files(stream,romtags,capacity);
  if(mark_)
    add_3dt_mark(stream,"packed and signed");

  fmt::print("  - Write layout ROM Tags\n");
  write_romtags(stream,romtags);
  update_romtags_file(stream,romtags.size() * sizeof(TDO::ROMTag));

  if(sign_payloads_)
    {
      sign_system_payloads(stream);
      sign_appsplash(stream);
    }

  if(stream.romtag(RSA_NEWKNEWNEWGNUBOOT))
    sign_disclabel_romtags_bootcode(stream);
  recreate_layout_signatures_file(stream);

  stream.close();
}

void
TDO::mark_disc_image(const std::filesystem::path &filepath_,
                     const std::string           &action_)
{
  TDO::FileStream stream;

  stream.open(filepath_,std::ios::in|std::ios::out);
  require_iso2048_image(stream);

  fmt::print("{}:\n",filepath_);
  add_3dt_mark(stream,action_);

  stream.close();
}

void
TDO::sign_disc_image(const std::filesystem::path &filepath_,
                     const bool                   mark_,
                     const bool                   preflight_,
                     const bool                   include_banner_romtag_,
                     const std::uint8_t           digest_check_count_)
{
  TDO::FileStream stream;

  stream.open(filepath_,std::ios::in|std::ios::out);
  require_iso2048_image(stream);

  fmt::print("{}:\n",filepath_);

  if(preflight_)
    preflight_signing_image(stream,include_banner_romtag_);

  pad_image_and_update_disclabel(stream);
  if(mark_)
    add_3dt_mark(stream,"signed");
  generate_and_write_romtags(stream,
                             include_banner_romtag_,
                             digest_check_count_);
  sign_system_payloads(stream);
  sign_appsplash(stream);
  resize_signatures_file_record(stream);
  sign_disclabel_romtags_bootcode(stream);
  recreate_layout_signatures_file(stream);

  stream.close();
}
