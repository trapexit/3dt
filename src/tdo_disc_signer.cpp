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
#include "tdo_romtag_metadata.hpp"
#include "tdo_rsa.hpp"
#include "version.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{
  // When false, the signing/packing helpers suppress their progress
  // output. Set by the public entry points from the caller's --verbose.
  static bool g_verbose = true;

  template<typename... Args>
  static
  void
  _vprint(const char *fmt_,
          Args&&...   args_)
  {
    if(g_verbose)
      fmt::print(fmt_,std::forward<Args>(args_)...);
  }

  static constexpr u32 ARM_NOP = 0xe1a00000;
  static constexpr u32 AIF_EXIT_INSTRUCTION = 0xef000011;
  static constexpr u32 AIF_RELOCATION_LIST_END = 0xffffffff;
  static constexpr u64 AIF_HEADER_RO_SIZE_OFFSET = 0x14;
  static constexpr u64 AIF_HEADER_RW_SIZE_OFFSET = 0x18;
  static constexpr u64 AIF_HEADER_DEBUG_SIZE_OFFSET = 0x1c;
  static constexpr u64 AIF_HEADER_WORKSPACE_OFFSET = 0x2c;
  static constexpr u64 AIF_3DO_FLAGS_OFFSET = 0xa4;
  static constexpr u64 AIF_3DO_SIGNATURE_OFFSET_OFFSET = 0xb0;
  static constexpr u64 AIF_3DO_SIGNATURE_LENGTH_OFFSET = 0xb4;
  static constexpr u64 AIF_3DO_HEADER_SIZE = 0xb8;
  static constexpr u32 AIF_3DO_HEADER_WORKSPACE_FLAG = 0x40000000;
  static constexpr u32 AIF_3DO_USERAPP = 0x04;
  static constexpr u64 COMPONENT_SIZE_OFFSET = 0x04;
  static constexpr char APP_SPLASH_PATTERN[] = "APPSCRN";

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
  boot_code_signed_size_from_decrypted(const std::vector<char> &data_)
  {
    std::optional<u64> executable_size;

    executable_size = aif_executable_size(data_);
    if(!executable_size)
      return {};

    return TDO::round_up(*executable_size,sizeof(u32)) + (RSA512_SIG_SIZE * 2);
  }

  static
  std::optional<u64>
  boot_code_romtag_size_from_decrypted(const std::vector<char> &data_)
  {
    std::optional<u64> signed_size;

    signed_size = boot_code_signed_size_from_decrypted(data_);
    if(!signed_size)
      return {};
    if(*signed_size > data_.size())
      return {};
    if((*signed_size < data_.size()) &&
       !is_zero_range(data_,*signed_size,data_.size()))
      return {};

    return signed_size;
  }

  static
  void
  decrypt_boot_code_data(std::vector<char> &data_)
  {
    const u64 aligned_size = TDO::boot_code_crypto_aligned_size(data_.size());

    TDO::decrypt_boot_code_range(data_.data(),aligned_size);
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
  apply_boot_romtag_version(TDO::ROMTag             &romtag_,
                            const std::vector<char> &decrypted_data_)
  {
    static constexpr u64 ITEMNODE_VERSION_OFFSET = 0x94;

    // M2 ROM recipes read Opera boot version/revision from AIF offsets
    // 0x94/0x95.  Some shipped encrypted boot_code payloads leave those
    // bytes zero after decryption; those cases are handled by the
    // hash-based fallback table.
    if(decrypted_data_.size() >= (ITEMNODE_VERSION_OFFSET + 2))
      {
        romtag_.version =
          static_cast<u8>(decrypted_data_[ITEMNODE_VERSION_OFFSET]);
        romtag_.revision =
          static_cast<u8>(decrypted_data_[ITEMNODE_VERSION_OFFSET + 1]);
      }
  }

  static
  void
  apply_romtag_version_revision_fallback(TDO::DevStream              &stream_,
                                         TDO::ROMTag                &romtag_,
                                         const TDO::DirectoryRecord &record_)
  {
    std::vector<char> data;
    const TDO::ROMTagVersionRevisionFallback *fallback;

    // For byte-count types the fallback table keys on the full on-disc
    // payload; the in-memory record_.byte_count may be stale since
    // boot_code size correction (update_record_sizes above) updates the
    // on-disc record but not this const reference -- so prefer
    // romtag_.size, which carries the corrected byte count for boot_code
    // and equals record_.byte_count for the other byte-count types.
    // 3dt emits the retail-compatible RSA_BLOCKS_ALWAYS form, whose size is
    // a block count rather than bytes, so use the filesystem byte_count for
    // payload hashing and metadata fallback.
    const u64 data_size = (romtag_.type == RSA_BLOCKS_ALWAYS)
      ? record_.byte_count
      : romtag_.size;

    // The disabled RSA_SIGNATURE_BLOCK placeholder deliberately has no
    // payload. There is nothing to hash for version/revision inference, and
    // DevStream's vector overload requires at least one destination byte.
    if(data_size == 0)
      return;

    if(data_size > static_cast<u64>(std::numeric_limits<s64>::max()))
      throw Error("ROMTag version/revision fallback file is too large");

    stream_.read_data_bytes_from_block(data,
                                       record_.avatar_list[0],
                                       static_cast<s64>(data_size));
    fallback = TDO::find_romtag_version_revision_fallback(romtag_.type,data);
    if(fallback == nullptr)
      {
        // A known payload hash is authoritative and may correct nonzero junk
        // in the generic AIF version offsets. If the oracle has no entry,
        // retain a plausible version already derived from the payload before
        // falling back to an existing on-disc ROMTag.
        if(TDO::romtag_has_version_revision(romtag_))
          return;

        // The hash oracle missed. This happens, among other cases, when
        // re-signing an image this tool already signed: replacing a payload's
        // trailing component signature changes the full-payload hash. As a
        // last resort, preserve the version/revision already recorded in the
        // on-disc ROMTag table for this type, keeping regenerated ROMTags
        // stable across sign runs (sign -> verify -> sign idempotency). On
        // retail discs this is the retail-authored value; on re-signed images
        // it is the value a prior sign established -- both authoritative.
        const std::optional<TDO::ROMTag> existing = stream_.romtag(romtag_.type);
        if(existing && TDO::romtag_has_version_revision(*existing))
          {
            romtag_.version   = existing->version;
            romtag_.revision = existing->revision;
          }
        return;
      }

    romtag_.version = fallback->version;
    romtag_.revision = fallback->revision;
    _vprint("    - using known {} version/revision {}.{} for file hash {}\n",
            TDO::ROMTag::type_str(romtag_.type),
            romtag_.version,
            romtag_.revision,
            fallback->md5);
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

  class SigningFSCallbacks : public TDO::FSWalker::Callbacks
  {
  public:
    Error
    invalid_filename(const std::filesystem::path &,
                     const std::string           &,
                     const TDO::DirectoryRecord  &,
                     const uint32_t,
                     const Error                 &,
                     TDO::DevStream              &) override
    {
      // Retail mastering output can contain otherwise harmless records with
      // path-separator names (the German Panasonic sampler is one example).
      // They cannot name any exact signing target below, and FSWalker cannot
      // safely recurse through them, so skip those records just as list and
      // unpack do instead of making an unrelated filename block re-signing.
      return Error();
    }
  };

  class ROMTagsFileUpdater final : public SigningFSCallbacks
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

      if(nonstd::string::as_lowercase(filepath_.generic_string()) != "rom_tags")
        return;

      update_record_sizes(stream_,
                          record_pos_,
                          romtags_file_size,
                          TDO::div_round_up(romtags_file_size,TDO::BLOCK_SIZE));
    }
  };

  class SignedAIFInspector final : public SigningFSCallbacks
  {
  private:
    static
    u64
    checked_avatar_byte_offset(const std::filesystem::path &filepath_,
                               TDO::DevStream              &stream_,
                               const u32                    avatar_,
                               const u64                    byte_count_)
    {
      const s64 image_size_s64 = stream_.size_in_bytes();
      const u64 byte_offset = static_cast<u64>(avatar_) * TDO::BLOCK_SIZE;

      if(image_size_s64 < 0)
        throw Error("invalid negative image size while inspecting AIF files");

      const u64 image_size = static_cast<u64>(image_size_s64);
      if((byte_offset > image_size) ||
         (byte_count_ > (image_size - byte_offset)))
        throw Error(fmt::format("AIF avatar range out of image: {}",
                                filepath_.string()));

      return byte_offset;
    }

    static
    void
    warn(const std::filesystem::path &filepath_,
         const std::string           &reason_)
    {
      fmt::print(stderr,
                 "3dt: warning: AIF {} {}; prepare it with modbin\n",
                 filepath_.generic_string(),
                 reason_);
    }

  public:
    void
    operator()(const std::filesystem::path &filepath_,
               const TDO::DirectoryRecord  &record_,
               const u32                    record_pos_,
               TDO::DevStream              &stream_)
    {
      md5_digest_t digest;
      rsa512_sig_t signature;
      std::vector<char> data;
      const char *key;
      u32 flags;
      u32 instruction;
      u32 signature_length;
      u32 signature_offset;
      u64 header_size;

      (void)record_pos_;

      const std::string lc_filepath =
        nonstd::string::as_lowercase(filepath_.generic_string());
      if((lc_filepath == "system/kernel/os_code") ||
         (lc_filepath == "system/kernel/misc_code") ||
         (lc_filepath == "system/kernel/boot_code"))
        return;

      if(record_.is_directory() ||
         record_.avatar_list.empty() ||
         (record_.byte_count < 0x40))
        return;

      header_size = std::min<u64>(record_.byte_count,AIF_3DO_HEADER_SIZE);
      checked_avatar_byte_offset(filepath_,
                                 stream_,
                                 record_.avatar_list[0],
                                 header_size);
      stream_.read_data_bytes_from_block(data,
                                         record_.avatar_list[0],
                                         header_size);

      if(read_u32_be(data,0x10) != AIF_EXIT_INSTRUCTION)
        return;
      instruction = read_u32_be(data,0x00);
      if((instruction != ARM_NOP) && !arm_bl_target(instruction,0x00))
        return;
      instruction = read_u32_be(data,0x04);
      if((instruction != ARM_NOP) && !arm_bl_target(instruction,0x04))
        return;
      instruction = read_u32_be(data,0x08);
      if((instruction != ARM_NOP) && !arm_bl_target(instruction,0x08))
        return;
      if(!arm_bl_target(read_u32_be(data,0x0c),0x0c))
        return;

      if(data.size() < AIF_3DO_HEADER_SIZE)
        {
          warn(filepath_,"has no complete 3DO header");
          return;
        }
      if((read_u32_be(data,AIF_HEADER_WORKSPACE_OFFSET) &
          AIF_3DO_HEADER_WORKSPACE_FLAG) == 0)
        {
          warn(filepath_,"has no 3DO header");
          return;
        }

      signature_offset = read_u32_be(data,AIF_3DO_SIGNATURE_OFFSET_OFFSET);
      signature_length = read_u32_be(data,AIF_3DO_SIGNATURE_LENGTH_OFFSET);
      if(signature_length == 0)
        {
          warn(filepath_,"is unsigned");
          return;
        }
      if((signature_length != RSA512_SIG_SIZE) ||
         (signature_offset < AIF_3DO_HEADER_SIZE) ||
         (signature_offset > record_.byte_count) ||
         (signature_length > (record_.byte_count - signature_offset)))
        {
          warn(filepath_,
               fmt::format("has unsupported signature metadata "
                           "(offset {}, length {})",
                           signature_offset,
                           signature_length));
          return;
        }

      data.clear();
      checked_avatar_byte_offset(filepath_,
                                 stream_,
                                 record_.avatar_list[0],
                                 static_cast<u64>(signature_offset) +
                                   signature_length);
      stream_.read_data_bytes_from_block(data,
                                         record_.avatar_list[0],
                                         static_cast<u64>(signature_offset) +
                                           signature_length);

      flags = read_u32_be(data,AIF_3DO_FLAGS_OFFSET);
      key = ((flags & AIF_3DO_USERAPP) != 0) ? TDO_KEY_APP : TDO_KEY_3DO;

      std::memcpy(signature,
                  data.data() + signature_offset,
                  sizeof(signature));

      // Portfolio authenticates ordinary AIFs independently of the disc
      // envelope. Preserve their bytes exactly: modbin owns AIF preparation,
      // while 3dt only reports signatures that retail systems will reject.
      std::fill(data.begin() + AIF_3DO_SIGNATURE_LENGTH_OFFSET,
                data.begin() + AIF_3DO_SIGNATURE_LENGTH_OFFSET + sizeof(u32),
                0);
      md5_calc(data.data(),signature_offset,digest);
      if(tdo_rsa_verify_retail(key,digest,signature))
        return;

      if(tdo_rsa_verify_development(digest,signature))
        {
          warn(filepath_,"has a development signature rejected by retail systems");
          return;
        }

      warn(filepath_,fmt::format("has an invalid {}-key signature",key));
    }
  };

  class SignaturesPlaceholderUpdater final : public SigningFSCallbacks
  {
  public:
    bool found = false;

  public:
    void
    operator()(const std::filesystem::path &filepath_,
               const TDO::DirectoryRecord  &record_,
               const u32                    record_pos_,
               TDO::DevStream              &stream_)
    {
      if(nonstd::string::as_lowercase(filepath_.generic_string()) != "signatures")
        return;

      if(record_.avatar_list.empty() || (record_.block_count == 0))
        throw Error("signatures placeholder has no allocated block");

      found = true;
      update_record_sizes(stream_,
                          record_pos_,
                          0,
                          record_.block_count);
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

  class SpecialFileCapacity final : public SigningFSCallbacks
  {
  public:
    bool found_rom_tags;
    bool found_signatures;
    u64  rom_tags_capacity;

  public:
    SpecialFileCapacity()
      : found_rom_tags(false),
        found_signatures(false),
        rom_tags_capacity(0)
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

      lc_filepath = nonstd::string::as_lowercase(filepath_.generic_string());
      if(lc_filepath == "rom_tags")
        {
          found_rom_tags = true;
          rom_tags_capacity = static_cast<u64>(record_.block_count) * record_.block_size;
        }
      else if(lc_filepath == "signatures")
        found_signatures = true;
    }
  };

  class SigningPreflight final : public SigningFSCallbacks
  {
  public:
    bool found_boot_code;
    bool found_misc_code;
    bool found_os_code;
    bool found_rom_tags;
    bool found_signatures;
    u32  generated_romtag_count;
    u64  rom_tags_capacity;
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

      lc_filepath = nonstd::string::as_lowercase(filepath_.generic_string());
      if(lc_filepath == "rom_tags")
        {
          found_rom_tags = true;
          rom_tags_capacity = static_cast<u64>(record_.block_count) * record_.block_size;
        }
      else if(lc_filepath == "signatures")
        found_signatures = true;
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

  class ROMTagsGenerator final : public SigningFSCallbacks
  {
  public:
    TDO::ROMTagVec romtags;
    bool           include_banner_romtag;
    bool           sign_payloads;
    const TDO::ROMTagVec &authoritative_romtags;
    const TDO::ROMTagVec &existing_romtags;

  public:
    ROMTagsGenerator(const bool            include_banner_romtag_,
                     const bool            sign_payloads_,
                     const TDO::ROMTagVec &authoritative_romtags_,
                     const TDO::ROMTagVec &existing_romtags_)
      : romtags(),
        include_banner_romtag(include_banner_romtag_),
        sign_payloads(sign_payloads_),
        authoritative_romtags(authoritative_romtags_),
        existing_romtags(existing_romtags_)
    {
    }

  private:
    u32
    size_hint(const TDO::ROMTagVec &romtags_,
              const u32             type_) const
    {
      for(const auto &tag : romtags_)
        {
          if(tag.type == type_)
            return tag.size;
          if((type_ == RSA_MISCCODE) && (tag.type == RSA_OLD_MISCCODE))
            return tag.size;
        }

      return 0;
    }

  public:
    void
    operator()(const std::filesystem::path &filepath_,
               const TDO::DirectoryRecord  &record_,
               const u32                    record_pos_,
               TDO::DevStream              &stream_)
    {
      u32 type;

      type = romtag_type_for_path(nonstd::string::as_lowercase(filepath_.generic_string()),
                                  include_banner_romtag);
      if(type == 0)
        return;

      if(record_.avatar_list.empty())
        throw Error(fmt::format("ROM tag candidate has no avatars: {}",
                                filepath_.generic_string()));
      if(record_.avatar_list[0] == 0)
        throw Error(fmt::format("ROM tag candidate has invalid avatar 0: {}",
                                filepath_.generic_string()));

      TDO::ROMTag romtag{};

      romtag.type        = type;
      romtag.sub_systype = RSANODE;
      romtag.size        = record_.byte_count;
      // TODO: Ordinary pack creates one avatar. Layout replay can preserve
      // additional avatar addresses, but ROMTag signing currently updates only
      // avatar_list[0]. Sign every copy if packing gains full secondary-avatar
      // support.
      romtag.offset      = record_.avatar_list[0] - 1;

      if(sign_payloads &&
         ((type == RSA_NEWKNEWNEWGNUBOOT) ||
          (type == RSA_OS) ||
          (type == RSA_MISCCODE) ||
          (type == RSA_APPSPLASH)))
        {
          u64 capacity;
          u32 inspection_size;
          std::vector<char> data;
          TDO::SignedROMTagPayloadLayout layout;

          capacity = static_cast<u64>(record_.block_count) * record_.block_size;
          if((record_.byte_count == 0) || (record_.byte_count > capacity))
            throw Error(fmt::format("invalid signed payload allocation: {}",
                                    filepath_.string()));
          inspection_size =
            ((type == RSA_APPSPLASH) ?
             std::min(record_.byte_count,TDO::APP_SPLASH_PAL_PAYLOAD_SIZE) :
             record_.byte_count);
          stream_.read_data_bytes_from_block(data,
                                             record_.avatar_list[0],
                                             inspection_size);
          layout = TDO::inspect_signed_romtag_payload(type,
                                                      data,
                                                      record_.byte_count,
                                                      size_hint(authoritative_romtags,type),
                                                      size_hint(existing_romtags,type));
          if(layout.signed_size > capacity)
            throw Error(fmt::format("{} needs {} bytes of signature storage but its allocation holds {} bytes; unpack and pack the image to rebuild it",
                                    filepath_.string(),
                                    layout.signed_size,
                                    capacity));
          if((type != RSA_APPSPLASH) &&
             (record_.byte_count > layout.payload_size) &&
             (record_.byte_count < layout.signed_size))
            throw Error(fmt::format("{} contains a partial signature trailer",
                                    filepath_.string()));

          if((type == RSA_APPSPLASH) &&
             (record_.byte_count != layout.signed_size))
            {
              _vprint("    - normalizing BannerScreen size to {}\n",
                      layout.signed_size);
              update_record_sizes(stream_,
                                  record_pos_,
                                  layout.signed_size,
                                  record_.block_count);
            }

          // Portfolio's ROMTag loaders hash the structural payload and read
          // the signature immediately after it.  The directory byte count is
          // not authoritative here: retail APPSPLASH records commonly expose
          // only the 153624-byte VideoImage while rt_Size includes the hidden
          // 64-byte trailer in the same 76-block allocation.
          romtag.size = layout.signed_size;
        }

      switch(type)
        {
        case RSA_SIGNATURE_BLOCK:
          // Portfolio requires this tag on APPDIGEST boot paths, but a zero
          // count returns success before reading the payload.
          romtag.type_specific = 0;
          romtag.size = 0;
          break;
        case RSA_BLOCKS_ALWAYS:
          // Portfolio documents table-relative offset plus byte size, but
          // its published LaunchMe check is inactive. Authentic retail
          // discs instead store the absolute avatar and block_count here.
          // Keep generating that mastering convention; verify accepts both
          // it and the Portfolio-documented form (see docs/romtags.md).
          romtag.offset = record_.avatar_list[0];
          romtag.size   = record_.block_count;
          break;
        case RSA_OS:
          apply_os_romtag_version(stream_,romtag,record_);
          break;
        case RSA_NEWKNEWNEWGNUBOOT:
          if(!sign_payloads)
            {
              u64 allocated_size;
              std::vector<char> buf;
              std::vector<char> decrypted;
              std::optional<u64> boot_size;

              allocated_size = static_cast<u64>(record_.block_count) * record_.block_size;
              stream_.read_data_bytes_from_block(buf,
                                                 record_.avatar_list[0],
                                                 allocated_size);

              decrypted = buf;
              decrypt_boot_code_data(decrypted);

              boot_size = boot_code_romtag_size_from_decrypted(decrypted);
              if(!boot_size)
                boot_size = TDO::round_up(record_.byte_count,sizeof(u32));
              apply_boot_romtag_version(romtag,decrypted);
              if(boot_size && (*boot_size != record_.byte_count))
                {
                  _vprint("    - correcting boot_code size to {}\n",
                          *boot_size);
                  romtag.size = *boot_size;
                  update_record_sizes(stream_,
                                      record_pos_,
                                      romtag.size,
                                      record_.block_count);
                }
            }
          else
            {
              std::vector<char> encrypted;

              stream_.read_data_bytes_from_block(encrypted,
                                                 record_.avatar_list[0],
                                                 record_.byte_count);
              decrypt_boot_code_data(encrypted);
              apply_boot_romtag_version(romtag,encrypted);
            }
          break;
        }

      apply_romtag_version_revision_fallback(stream_,romtag,record_);
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
  update_disclabel(TDO::FileStream &stream_)
  {
    TDO::DiscLabel dl;

    _vprint("  - Update disc label\n");

    dl = stream_.disc_label();
    {
      // size_in_device_blocks() returns s64. dl.volume_block_count is
      // u32 (the OperaFS disc-label field type). Range-check before
      // narrowing so a legitimately-too-large image, or the -1 sentinel
      // propagated from a failed tellg, becomes a loud error rather
      // than silent truncation that ships a signed image whose label
      // disagrees with its actual size.
      const s64 blocks = stream_.size_in_device_blocks();
      if((blocks < 0) ||
         (blocks > static_cast<s64>(std::numeric_limits<uint32_t>::max())))
        throw Error("image device block count does not fit in disc label volume_block_count (uint32)");
      dl.volume_block_count = static_cast<uint32_t>(blocks);
    }

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

     _vprint("  - Setting location {} to '{}'\n",
             mark_offset,
             mark.c_str());
    stream_.data_byte_seek(mark_offset);
    stream_.write(mark.c_str(),mark.size());
  }

  // True for ROM tag types whose `offset` field is calculated by this
  // tool as (first_data_block - 1) and so must satisfy the bounds
  // enforced by safe_romtag_first_data_block. Other tag types (e.g.
  // RSA_BILLSTUFF, which stores a unique-id XOR; RSA_BLOCKS_ALWAYS,
  // for which 3dt emits the retail absolute-avatar form) carry
  // domain-specific values in `offset` and must not be passed through
  // that helper.
  static
  bool
  romtag_offset_is_block_index_minus_one(const TDO::ROMTag &tag_)
  {
    switch(tag_.type)
      {
      case RSA_OS:
      case RSA_MISCCODE:
      case RSA_NEWKNEWNEWGNUBOOT:
      case RSA_APPSPLASH:
      case RSA_SIGNATURE_BLOCK:
        return true;
      default:
        return false;
      }
  }

  static
  void
  write_romtags(TDO::FileStream      &stream_,
                const TDO::ROMTagVec &romtags_)
  {
    stream_.data_block_seek(stream_.romtags_block());
    for(auto &tag : romtags_)
      {
        if(romtag_offset_is_block_index_minus_one(tag))
          _vprint("    - type: {}; offset: {}; size: {}b\n",
                  TDO::ROMTag::type_str(tag.type),
                  TDO::safe_romtag_first_data_block(stream_,tag,
                                                    TDO::ROMTag::type_str(tag.type).c_str()),
                  tag.size);
        else if(tag.type == RSA_BLOCKS_ALWAYS)
          _vprint("    - type: {}; offset: {:#010x}; size: {} blocks\n",
                  TDO::ROMTag::type_str(tag.type),
                  tag.offset,
                  tag.size);
        else
          _vprint("    - type: {}; offset: {:#010x}; size: {}b\n",
                  TDO::ROMTag::type_str(tag.type),
                  tag.offset,
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
  void
  apply_source_romtag_versions(TDO::ROMTagVec       &romtags_,
                               const TDO::ROMTagVec &source_romtags_)
  {
    for(auto &romtag : romtags_)
      {
        const auto source = std::find_if(source_romtags_.begin(),
                                         source_romtags_.end(),
                                         [&romtag](const TDO::ROMTag &candidate_)
                                         {
                                           return ((candidate_.sub_systype == romtag.sub_systype) &&
                                                   (candidate_.type == romtag.type));
                                         });
        if(source == source_romtags_.end())
          continue;

        // Repack/unpack-pack carry a known-good source table, and direct sign
        // starts from the table already in the image.  Those version/revision
        // fields are authoritative and intentionally override payload
        // heuristics and the MD5 oracle. All structural fields are freshly
        // regenerated.
        romtag.version = source->version;
        romtag.revision = source->revision;
      }
  }

  static
  TDO::ROMTagVec
  generate_romtags_for_image(TDO::FileStream &stream_,
                             const bool       include_banner_romtag_,
                             const bool       include_billstuff_romtag_,
                             const bool       sign_payloads_,
                             const TDO::ROMTagVec &source_romtags_)
  {
    TDO::ROMTagVec trusted_romtags(source_romtags_);
    const TDO::ROMTagVec existing_romtags = stream_.romtags();

    for(const auto &existing : existing_romtags)
      {
        const auto duplicate =
          std::find_if(trusted_romtags.begin(),
                       trusted_romtags.end(),
                       [&existing](const TDO::ROMTag &candidate_)
                       {
                         return (candidate_.type == existing.type);
                       });
        if(duplicate == trusted_romtags.end())
          trusted_romtags.emplace_back(existing);
      }

    ROMTagsGenerator tags(include_banner_romtag_,
                          sign_payloads_,
                          source_romtags_,
                          existing_romtags);
    TDO::FSWalker fswalker(stream_,tags,false);

    fswalker.walk();

    if(include_billstuff_romtag_)
      add_billstuff_romtag(stream_,tags.romtags);
    apply_source_romtag_versions(tags.romtags,trusted_romtags);
    sort_romtags(tags.romtags);

    return tags.romtags;
  }

  static
  void
  preflight_layout_special_files(const TDO::ROMTagVec      &romtags_,
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
      throw Error("generated ROMTags are missing RSA_SIGNATURE_BLOCK");

    if(!capacity_.found_signatures)
      throw Error("image is missing file: signatures");
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
  reset_signatures_placeholder(TDO::FileStream &stream_)
  {
    SignaturesPlaceholderUpdater updater;
    TDO::FSWalker fsw(stream_,updater);

    fsw.walk();
    if(!updater.found)
      throw Error("image is missing file: signatures");
  }

  static
  void
  preflight_signing_image(TDO::FileStream &stream_,
                          const bool       include_banner_romtag_,
                          const bool       include_billstuff_romtag_)
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
    if(include_billstuff_romtag_)
      preflight.generated_romtag_count++;

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
                             const bool       include_billstuff_romtag_,
                             const TDO::ROMTagVec &source_romtags_)
  {
    TDO::ROMTagVec romtags;

    _vprint("  - Generate and write ROM Tags\n");
    romtags = generate_romtags_for_image(stream_,
                                         include_banner_romtag_,
                                         include_billstuff_romtag_,
                                         true,
                                         source_romtags_);
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

    const u64 first_block = safe_romtag_payload_range(stream_,
                                                      *romtag,
                                                      romtag->size,
                                                      label_);

    stream_.read_data_bytes_from_block(data,
                                       first_block,
                                       romtag->size);

    data.resize(romtag->size - RSA512_SIG_SIZE);
    // CD-ROM ROMTag asset checks (cdromdipir.c:ReadOsComponent)
    // hash the raw payload up to the trailing signature. RSACheck's
    // _3DO_SignatureLen zeroing applies to AIF task/driver checks, not
    // these ROMTag signatures.
    md5_calc(data.data(),data.size(),digest);
    tdo_rsa_sign(key_,digest,sig);

    _vprint("  - Signing {}\n"
            "    - MD5 digest: {}\n"
            "    - RSA signature: {}\n",
            label_,
            digest,
            sig);

    stream_.data_byte_seek((first_block * TDO::BLOCK_SIZE) +
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

    const u64 first_block = safe_romtag_payload_range(stream_,
                                                      *romtag,
                                                      romtag->size,
                                                      "boot_code");
    const u64 post_cheeze_sig_offset = romtag->size - (RSA512_SIG_SIZE * 2);
    const u64 outer_sig_offset = romtag->size - RSA512_SIG_SIZE;
    stream_.read_data_bytes_from_block(data,
                                       first_block,
                                       romtag->size);

    TDO::decrypt_boot_code_range(data.data(),post_cheeze_sig_offset);
    md5_calc(data.data(),post_cheeze_sig_offset,digest);
    tdo_rsa_sign(TDO_KEY_3DO,digest,sig);

    _vprint("  - Signing decrypted boot_code\n"
            "    - MD5 digest: {}\n"
            "    - RSA signature: {}\n",
            digest,
            sig);

    std::memcpy(encrypted_sig.data(),sig,sizeof(sig));
    TDO::encrypt_boot_code_range(encrypted_sig.data(),
                                 encrypted_sig.size(),
                                 post_cheeze_sig_offset);

    stream_.data_byte_seek((first_block * TDO::BLOCK_SIZE) +
                           post_cheeze_sig_offset);
    stream_.write(encrypted_sig.data(),encrypted_sig.size());

    data.clear();
    stream_.read_data_bytes_from_block(data,
                                       first_block,
                                       outer_sig_offset);
    md5_calc(data.data(),data.size(),digest);
    tdo_rsa_sign(TDO_KEY_3DO,digest,sig);

    _vprint("  - Signing encrypted boot_code\n"
            "    - MD5 digest: {}\n"
            "    - RSA signature: {}\n",
            digest,
            sig);

    stream_.data_byte_seek((first_block * TDO::BLOCK_SIZE) +
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
  void
  inspect_aif_files(TDO::FileStream &stream_)
  {
    SignedAIFInspector inspector;
    TDO::FSWalker fsw(stream_,inspector,false);

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
                                       safe_romtag_first_data_block(stream_,*romtag,"boot_code"),
                                       romtag->size);

    md5_calc(data.data(),data.size(),digest);
    tdo_rsa_sign(TDO_KEY_APP,digest,signature);

    _vprint("  - Signing DiscLabel + ROMTags + BootCode with APP key\n"
            "    - MD5 digest: {}\n"
            "    - RSA signature: {}\n",
            digest,
            signature);

    stream_.data_block_seek(stream_.romtags_block());
    stream_.data_byte_skip(stream_.romtags_size_in_bytes());
    stream_.write((char*)signature,sizeof(signature));
  }
}

TDO::SignedROMTagPayloadLayout
TDO::inspect_signed_romtag_payload(const u32                romtag_type_,
                                   const std::vector<char> &data_,
                                   const u32                logical_size_hint_,
                                   const u32                authoritative_size_hint_,
                                   const u32                existing_size_hint_)
{
  u64 payload_size;
  u64 signed_size;
  u64 signature_size;

  payload_size = 0;
  signed_size = 0;
  signature_size = RSA512_SIG_SIZE;

  switch(romtag_type_)
    {
    case RSA_APPSPLASH:
      {
        if(data_.size() < TDO::APP_SPLASH_NTSC_PAYLOAD_SIZE)
          throw Error("BannerScreen is smaller than the standard 153624-byte payload");
        if((static_cast<u8>(data_[0]) != 1) ||
           (std::memcmp(data_.data() + 1,
                        APP_SPLASH_PATTERN,
                        sizeof(APP_SPLASH_PATTERN) - 1) != 0))
          throw Error("BannerScreen has an invalid VideoImage header");

        const auto is_pal_extent = [](const u32 size_)
        {
          return ((size_ == TDO::APP_SPLASH_PAL_PAYLOAD_SIZE) ||
                  (size_ == TDO::APP_SPLASH_PAL_SIGNED_SIZE));
        };

        // NTSC and PAL mastering use distinct fixed extents. Do not derive the
        // extent from VideoImage geometry: shipping Retro Love Letter banners
        // contain stale header values. Exact source or ROMTag extents reliably
        // identify PAL; other oversized inputs retain the established NTSC
        // truncation policy.
        payload_size =
          (is_pal_extent(logical_size_hint_) ||
           is_pal_extent(authoritative_size_hint_) ||
           is_pal_extent(existing_size_hint_)) ?
          TDO::APP_SPLASH_PAL_PAYLOAD_SIZE :
          TDO::APP_SPLASH_NTSC_PAYLOAD_SIZE;
      }
      break;

    case RSA_OS:
    case RSA_MISCCODE:
    case RSA_OLD_MISCCODE:
      // makeboot stores the unsigned component extent in the big-endian word
      // at +4. Portfolio's ReadOsComponent hashes rt_Size - 64 bytes, then
      // reads the signature immediately after that declared payload.
      if(data_.size() < (COMPONENT_SIZE_OFFSET + sizeof(u32)))
        throw Error("system component is too small to contain its size header");
      payload_size = read_u32_be(data_,COMPONENT_SIZE_OFFSET);
      if(payload_size < (COMPONENT_SIZE_OFFSET + sizeof(u32)))
        throw Error("system component has an invalid payload size");
      break;

    case RSA_NEWKNEWNEWGNUBOOT:
      {
        std::vector<char> decrypted(data_);
        std::optional<u64> derived_size;

        signature_size = RSA512_SIG_SIZE * 2;
        decrypt_boot_code_data(decrypted);
        derived_size = boot_code_signed_size_from_decrypted(decrypted);
        if(derived_size)
          {
            u64 minimum_payload_size;

            minimum_payload_size = *derived_size - signature_size;
            // Some makeboot inputs retain one zero word immediately after the
            // relocation terminator.  It belongs to the payload, not to the
            // variable amount of zero padding that may precede the slots.
            if(((minimum_payload_size + sizeof(u32)) <= decrypted.size()) &&
               (read_u32_be(decrypted,minimum_payload_size) == 0))
              minimum_payload_size += sizeof(u32);

            auto valid_declared_extent = [&](const u64 candidate_size_)
            {
              if(candidate_size_ < signature_size)
                return false;
              const u64 candidate_payload_size = candidate_size_ - signature_size;
              return ((candidate_payload_size >= minimum_payload_size) &&
                      (candidate_payload_size <= decrypted.size()));
            };

            auto has_valid_trailing_signatures = [&](const u64 candidate_size_)
            {
              md5_digest_t digest;
              rsa512_sig_t expected;

              // Portfolio development Dipir accepts its demo and engineering
              // public keys. Those signatures are useful here only as proof
              // that this extent already reserves both slots; 3dt replaces
              // them with retail signatures before emitting the image.
              if((candidate_size_ < signature_size) ||
                 (candidate_size_ > data_.size()))
                return false;

              const u64 inner_offset = candidate_size_ - signature_size;
              const u64 outer_offset = candidate_size_ - RSA512_SIG_SIZE;
              md5_calc(decrypted.data(),inner_offset,digest);
              tdo_rsa_sign(TDO_KEY_3DO,digest,expected);
              const auto *inner_signature =
                reinterpret_cast<const rsa512_sig_t*>(decrypted.data() + inner_offset);
              if((std::memcmp(expected,
                              *inner_signature,
                              sizeof(expected)) != 0) &&
                 !tdo_rsa_verify_development(digest,*inner_signature))
                return false;

              md5_calc(data_.data(),outer_offset,digest);
              tdo_rsa_sign(TDO_KEY_3DO,digest,expected);
              const auto *outer_signature =
                reinterpret_cast<const rsa512_sig_t*>(data_.data() + outer_offset);
              return ((std::memcmp(expected,
                                   *outer_signature,
                                   sizeof(expected)) == 0) ||
                      tdo_rsa_verify_development(digest,*outer_signature));
            };

            auto has_zero_trailing_slots = [&](const u64 candidate_size_)
            {
              return ((candidate_size_ >= signature_size) &&
                      (candidate_size_ <= data_.size()) &&
                      is_zero_range(data_,
                                    candidate_size_ - signature_size,
                                    candidate_size_));
            };

            const bool known_source_payload =
              (TDO::find_romtag_version_revision_fallback(RSA_NEWKNEWNEWGNUBOOT,
                                                          data_) != nullptr);

            // Portfolio's padded 8 KiB beta boot components cannot be reduced
            // to minimum_payload_size+128: their authoritative ROMTag places
            // both signatures after a pad.  For standalone source files, a
            // valid existing pair or an all-zero reserved tail proves the same
            // layout.  Otherwise append slots after every source byte so an
            // unsigned file cannot lose payload merely because it is padded.
            auto extent_has_storage = [&](const u64 candidate_size_)
            {
              if(!valid_declared_extent(candidate_size_))
                return false;
              const u64 candidate_payload_size = candidate_size_ - signature_size;
              return ((candidate_payload_size == minimum_payload_size) ||
                      known_source_payload ||
                      has_valid_trailing_signatures(candidate_size_) ||
                      has_zero_trailing_slots(candidate_size_));
            };

            if((authoritative_size_hint_ != 0) &&
               extent_has_storage(authoritative_size_hint_))
              signed_size = authoritative_size_hint_;
            else if((existing_size_hint_ != 0) &&
                    extent_has_storage(existing_size_hint_))
              signed_size = existing_size_hint_;
            else if((logical_size_hint_ != 0) &&
                    extent_has_storage(logical_size_hint_))
              signed_size = logical_size_hint_;
            else if((logical_size_hint_ >= minimum_payload_size) &&
                    (logical_size_hint_ <= decrypted.size()))
              signed_size = static_cast<u64>(logical_size_hint_) + signature_size;
            else
              signed_size = minimum_payload_size + signature_size;

            payload_size = signed_size - signature_size;
          }
        else
          {
            throw Error("boot_code signed extent cannot be derived safely");
          }
      }
      break;

    default:
      throw Error("unsupported signed ROMTag payload type");
    }

  if(signed_size == 0)
    signed_size = payload_size + signature_size;
  if((payload_size > data_.size()) ||
     (payload_size > std::numeric_limits<u32>::max()) ||
     (signed_size > std::numeric_limits<u32>::max()))
    throw Error("signed ROMTag payload size is outside the available data");

  return {static_cast<u32>(payload_size),static_cast<u32>(signed_size)};
}

void
TDO::recreate_layout_special_files(const std::filesystem::path &filepath_,
                                   const bool                   sign_payloads_,
                                   const bool                   mark_,
                                   const bool                   include_banner_romtag_,
                                   const bool                   include_billstuff_romtag_,
                                   const TDO::ROMTagVec        &source_romtags_,
                                   const bool                   verbose_)
{
  SpecialFileCapacity capacity;
  TDO::ROMTagVec romtags;
  TDO::FileStream stream;

  g_verbose = verbose_;
  stream.open(filepath_,std::ios::in|std::ios::out);
  require_iso2048_image(stream);

  _vprint("{}:\n",filepath_);
  _vprint("  - Recreate layout special files\n");

  {
    TDO::FSWalker fsw(stream,capacity,false);

    fsw.walk();
  }
  update_disclabel(stream);
  reset_signatures_placeholder(stream);
  romtags = generate_romtags_for_image(stream,
                                       include_banner_romtag_,
                                       include_billstuff_romtag_,
                                       sign_payloads_,
                                       source_romtags_);
  preflight_layout_special_files(romtags,capacity);
  if(mark_)
    add_3dt_mark(stream,"packed and signed");

  _vprint("  - Write layout ROM Tags\n");
  write_romtags(stream,romtags);
  update_romtags_file(stream,romtags.size() * sizeof(TDO::ROMTag));

  if(sign_payloads_)
    {
      sign_system_payloads(stream);
      inspect_aif_files(stream);
      sign_appsplash(stream);
    }

  if(sign_payloads_ && stream.romtag(RSA_NEWKNEWNEWGNUBOOT))
    sign_disclabel_romtags_bootcode(stream);

  stream.close();
}

void
TDO::mark_disc_image(const std::filesystem::path &filepath_,
                     const std::string           &action_,
                     const bool                   verbose_)
{
  TDO::FileStream stream;

  g_verbose = verbose_;
  stream.open(filepath_,std::ios::in|std::ios::out);
  require_iso2048_image(stream);

  _vprint("{}:\n",filepath_);
  add_3dt_mark(stream,action_);

  stream.close();
}

void
TDO::sign_disc_image(const std::filesystem::path &filepath_,
                     const bool                   mark_,
                     const bool                   preflight_,
                     const bool                   include_banner_romtag_,
                     const bool                   include_billstuff_romtag_,
                     const TDO::ROMTagVec        &source_romtags_,
                     const bool                   verbose_)
{
  TDO::FileStream stream;

  g_verbose = verbose_;
  stream.open(filepath_,std::ios::in|std::ios::out);
  require_iso2048_image(stream);

  _vprint("{}:\n",filepath_);

  if(preflight_)
    preflight_signing_image(stream,
                            include_banner_romtag_,
                            include_billstuff_romtag_);

  update_disclabel(stream);
  if(mark_)
    add_3dt_mark(stream,"signed");
  reset_signatures_placeholder(stream);
  generate_and_write_romtags(stream,
                             include_banner_romtag_,
                             include_billstuff_romtag_,
                             source_romtags_);
  sign_system_payloads(stream);
  inspect_aif_files(stream);
  sign_appsplash(stream);
  sign_disclabel_romtags_bootcode(stream);

  stream.close();
}
