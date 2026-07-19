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

#include "md5.h"
#include "subcmd.hpp"

#include "options.hpp"
#include "tdo_dev_stream.hpp"
#include "tdo_disc_format.hpp"
#include "tdo_disc_label.hpp"
#include "tdo_romtag.hpp"
#include "tdo_romtag_metadata.hpp"
#include "tdo_boot_code_crypto.hpp"
#include "tdo_file_stream.hpp"
#include "tdo_fs_walker.hpp"
#include "tdo_rsa.hpp"

#include "discdata.h"

#include "fmt.hpp"
#include "fmt_md5_digest.hpp"
#include "fmt_rsa512_sig.hpp"
#include "json.hpp"
#include "nonstd/string.hpp"

#include "types_ints.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

static constexpr int VERIFY_EXIT_UNSIGNED = 2;
static constexpr int VERIFY_EXIT_UNSUPPORTED = 3;
static constexpr int VERIFY_EXIT_INVALID = 4;

static bool g_quiet = false;

enum class VerifyStatus
  {
    Valid,
    Unsigned,
    Unsupported,
    Invalid
  };

struct VerifyResult
{
  std::string  path;
  VerifyStatus status;
};

static
const char*
verify_status_str(const VerifyStatus status_)
{
  switch(status_)
    {
    case VerifyStatus::Valid:
      return "valid";
    case VerifyStatus::Unsigned:
      return "unsigned";
    case VerifyStatus::Unsupported:
      return "unsupported";
    case VerifyStatus::Invalid:
      return "invalid";
    }

  return "invalid";
}

template<typename... Args>
static
void
_vprint(const char *fmt_,
        Args&&...   args_)
{
  if(!g_quiet)
    fmt::print(fmt_,std::forward<Args>(args_)...);
}

static
bool
_range_in_image(TDO::DevStream &s_,
                const s64       image_size_,
                const u64       start_block_,
                const u64       byte_count_)
{
  s64 file_offset;

  if(byte_count_ == 0)
    return true;

  file_offset = s_.data_block_to_file_offset(start_block_);
  if((file_offset < 0) || (image_size_ < 0))
    return false;
  if(file_offset > image_size_)
    return false;

  return (byte_count_ <= static_cast<u64>(image_size_ - file_offset));
}

class VerifyFSCallbacks final : public TDO::FSWalker::Callbacks
{
public:
  Error
  invalid_filename(const std::filesystem::path &parent_,
                   const std::string           &filename_,
                   const TDO::DirectoryRecord&,
                   const uint32_t,
                   const Error                 &err_,
                   TDO::DevStream&)
  {
    fmt::print(stderr,
               "3dt: warning: {} - {}\n",
               err_.str,
               TDO::display_path(parent_,filename_));

    return {};
  }
};

struct ROMTagMetadataRecord
{
  bool                  found = false;
  std::filesystem::path path;
  TDO::DirectoryRecord  record;
};

class ROMTagMetadataRecordCollector final : public TDO::FSWalker::Callbacks
{
public:
  ROMTagMetadataRecord boot_code;
  ROMTagMetadataRecord misc_code;
  ROMTagMetadataRecord os_code;
  ROMTagMetadataRecord launchme;

public:
  void
  operator()(const std::filesystem::path &filepath_,
             const TDO::DirectoryRecord  &record_,
             const uint32_t,
             TDO::DevStream&)
  {
    collect(filepath_,record_);
  }

  Error
  invalid_filename(const std::filesystem::path &parent_,
                   const std::string           &filename_,
                   const TDO::DirectoryRecord&,
                   const uint32_t,
                   const Error                 &err_,
                   TDO::DevStream&)
  {
    fmt::print(stderr,
               "3dt: warning: {} - {}\n",
               err_.str,
               TDO::display_path(parent_,filename_));

    return {};
  }

private:
  void
  collect(const std::filesystem::path &filepath_,
          const TDO::DirectoryRecord  &record_)
  {
    ROMTagMetadataRecord *metadata;
    const std::string lc_filepath = nonstd::string::as_lowercase(filepath_.generic_string());

    metadata = nullptr;
    if(lc_filepath == "system/kernel/boot_code")
      metadata = &boot_code;
    else if(lc_filepath == "system/kernel/misc_code")
      metadata = &misc_code;
    else if(lc_filepath == "system/kernel/os_code")
      metadata = &os_code;
    else if(lc_filepath == "launchme")
      metadata = &launchme;

    if(metadata == nullptr)
      return;

    metadata->found = true;
    metadata->path = filepath_;
    metadata->record = record_;
  }
};

static
bool
_read_metadata_record_data(TDO::DevStream             &s_,
                           const ROMTagMetadataRecord &metadata_,
                           std::vector<char>          &data_)
{
  const TDO::DirectoryRecord &record = metadata_.record;

  if(record.avatar_list.empty())
    {
      _vprint("   - error: {} has no avatars\n",
              metadata_.path.generic_string());
      return false;
    }
  if(!_range_in_image(s_,s_.size_in_bytes(),record.avatar_list[0],record.byte_count))
    {
      _vprint("   - error: {} is outside image bounds\n",
              metadata_.path.generic_string());
      return false;
    }

  s_.read_data_bytes_from_block(data_,
                                record.avatar_list[0],
                                record.byte_count);

  return true;
}

static
bool
_verify_romtag_version_revision_fallback(TDO::DevStream                   &s_,
                                         const std::optional<TDO::ROMTag> &romtag_,
                                         const ROMTagMetadataRecord       &metadata_)
{
  const TDO::ROMTagVersionRevisionFallback *fallback;
  std::vector<char> data;

  if(metadata_.found && metadata_.record.byte_count == 0)
    {
      _vprint("   - error: {} has zero length\n",
              metadata_.path.generic_string());
      return false;
    }
  if(!romtag_ || !metadata_.found)
    return true;
  if(TDO::romtag_has_version_revision(*romtag_))
    return true;
  if(!_read_metadata_record_data(s_,metadata_,data))
    return false;

  fallback = TDO::find_romtag_version_revision_fallback(romtag_->type,data);
  if(fallback == nullptr)
    return true;

  _vprint("   - error: {} ROMTag version/revision is {}.{} for {}; known file hash {} expects {}.{}\n",
          romtag_->type_str(),
          romtag_->version,
          romtag_->revision,
          metadata_.path.generic_string(),
          fallback->md5,
          fallback->version,
          fallback->revision);

  return false;
}

static
bool
_verify_blocks_always_romtag(TDO::DevStream                   &s_,
                             const std::optional<TDO::ROMTag> &romtag_,
                             const ROMTagMetadataRecord       &metadata_)
{
  bool portfolio_encoding;
  bool portfolio_offset_valid;
  bool retail_encoding;
  u64 portfolio_offset;
  const TDO::DirectoryRecord &record = metadata_.record;

  if(!romtag_ || !metadata_.found)
    return true;

  if(record.avatar_list.empty())
    {
      _vprint("   - error: {} has no avatars\n",
              metadata_.path.generic_string());
      return false;
    }
  if(record.avatar_list[0] == 0)
    {
      _vprint("   - error: {} has invalid avatar 0\n",
              metadata_.path.generic_string());
      return false;
    }

  // Portfolio's src/includes/rom.h defines rt_Offset as a block offset from
  // the ROMTag table and rt_Size as bytes. src/dipir/cdromdipir.c applies
  // those generic rules to RSA_BLOCKS_ALWAYS, but only under SIGN_LAUNCHME.
  // Its published RSACheckLaunchme() returns success before reading the
  // saved size is never consumed. Authentic retail discs therefore also
  // boot with the mastering convention seen in the corpus: absolute avatar
  // plus block_count. Accept both complete pairs, but not a hybrid of them.
  portfolio_offset_valid = (record.avatar_list[0] >= s_.romtags_block());
  portfolio_offset = portfolio_offset_valid
    ? (record.avatar_list[0] - s_.romtags_block())
    : 0;
  portfolio_encoding =
    (portfolio_offset_valid &&
     (romtag_->offset == portfolio_offset) &&
     (romtag_->size == record.byte_count));
  retail_encoding =
    ((romtag_->offset == record.avatar_list[0]) &&
     (romtag_->size == record.block_count));

  if(portfolio_encoding)
    {
      _vprint("   - BLOCKS_ALWAYS encoding: Portfolio (table-relative offset, byte size)\n");
      return true;
    }
  if(retail_encoding)
    {
      _vprint("   - BLOCKS_ALWAYS encoding: retail compatibility (absolute offset, block count)\n");
      return true;
    }

  if(portfolio_offset_valid)
    _vprint("   - error: BLOCKS_ALWAYS ROMTag is offset {}, size {}; expected Portfolio offset {}, size {} bytes or retail offset {}, size {} blocks for {}\n",
            romtag_->offset,
            romtag_->size,
            portfolio_offset,
            record.byte_count,
            record.avatar_list[0],
            record.block_count,
            metadata_.path.generic_string());
  else
    _vprint("   - error: BLOCKS_ALWAYS ROMTag is offset {}, size {}; LaunchMe block {} precedes ROMTag table block {} and does not match retail offset {}, size {} blocks for {}\n",
            romtag_->offset,
            romtag_->size,
            record.avatar_list[0],
            s_.romtags_block(),
            record.avatar_list[0],
            record.block_count,
            metadata_.path.generic_string());

  return false;
}

static
bool
_verify_romtag_metadata(TDO::DevStream &s_)
{
  bool matched;
  ROMTagMetadataRecordCollector records;
  TDO::FSWalker fsw(s_,records,false);

  _vprint(" - Verifying ROMTag metadata\n");

  fsw.walk();

  matched = true;
  matched &= _verify_romtag_version_revision_fallback(s_,
                                                      s_.romtag(RSA_NEWKNEWNEWGNUBOOT),
                                                      records.boot_code);
  matched &= _verify_romtag_version_revision_fallback(s_,
                                                      s_.romtag(RSA_MISCCODE),
                                                      records.misc_code);
  matched &= _verify_romtag_version_revision_fallback(s_,
                                                      s_.romtag(RSA_OS),
                                                      records.os_code);
  matched &= _verify_blocks_always_romtag(s_,
                                          s_.romtag(RSA_BLOCKS_ALWAYS),
                                          records.launchme);

  return matched;
}

static
void
_get_cross_app_sig(TDO::DevStream &s_,
                   rsa512_sig_t    sig_)
{
  s_.data_block_seek(s_.romtags_block());
  s_.data_byte_skip(s_.romtags_size_in_bytes());
  s_.read((char*)sig_,sizeof(rsa512_sig_t));
}

static
void
_get_sig_from_end(std::vector<char> &data_,
                  rsa512_sig_t       sig_)
{
  std::memcpy(sig_,
              &data_[data_.size() - sizeof(rsa512_sig_t)],
              sizeof(rsa512_sig_t));
}

static
bool
_is_zero_sig(const rsa512_sig_t sig_)
{
  static const std::array<unsigned char, RSA512_SIG_SIZE> ZERO{};
  return (std::memcmp(sig_, ZERO.data(), RSA512_SIG_SIZE) == 0);
}

// Unsigned prerelease discs can use recognizable placeholders instead of real
// RSA signatures. Casper beta uses "iamaduck" for the cross-app signature.
static
bool
_is_iamaduck_sig(const rsa512_sig_t sig_)
{
  static constexpr char PATTERN[] = "iamaduck";

  for(u64 i = 0; i < sizeof(rsa512_sig_t); i++)
    if(sig_[i] != PATTERN[i % (sizeof(PATTERN) - 1)])
      return false;

  return true;
}

static
const char*
_sig_status(const rsa512_sig_t sig_,
            const bool         matched_,
            bool              &saw_unsigned_,
            bool              &saw_invalid_)
{
  if(matched_)
    return "valid";
  if(_is_zero_sig(sig_))
    {
      saw_unsigned_ = true;
      return "unsigned placeholder: zero";
    }
  if(_is_iamaduck_sig(sig_))
    {
      saw_unsigned_ = true;
      return "unsigned placeholder: iamaduck";
    }

  saw_invalid_ = true;
  return "invalid";
}


// See details in `portfolio_os/src/dipir/cdipir.c:1178` from the original Portfolio OS tree.
static
bool
_verify_disclabel_romtags_bootcode(TDO::DevStream &s_,
                                   bool           &saw_unsigned_,
                                   bool           &saw_invalid_)
{
  md5_digest_t digest;
  rsa512_sig_t original_sig;
  rsa512_sig_t computed_sig;
  std::vector<char> data;
  std::optional<TDO::ROMTag> romtag;

  _vprint(" - Verifying DiscLabel + ROMTags + BootCode with APP Key\n");

  s_.read_data_bytes_from_block(data,
                                s_.disc_label_block(),
                                s_.disc_label_size_in_bytes());
  s_.read_data_bytes_from_block(data,
                                s_.romtags_block(),
                                s_.romtags_size_in_bytes());

  romtag = s_.romtag(RSA_NEWKNEWNEWGNUBOOT);
  if(!romtag)
    {
      _vprint(" - No NEWKNEWNEWGNUBOOT romtag found.\n");
      return true;
    }
  const u64 newgnuboot_first_block = TDO::safe_romtag_first_data_block(s_,*romtag,"NEWKNEWNEWGNUBOOT");
  if(!_range_in_image(s_,s_.size_in_bytes(),newgnuboot_first_block,romtag->size))
    {
      _vprint("   - error: NEWKNEWNEWGNUBOOT is outside image bounds\n");
      return false;
    }

  s_.read_data_bytes_from_block(data,
                                newgnuboot_first_block,
                                romtag->size);

  _vprint("   - disc label block: {}\n"
          "   - disc label size: {}b\n"
          "   - romtags block: {}\n"
          "   - romtags size: {}b\n"
          "   - newknewnewgnuboot block: {}\n"
          "   - newknewnewgnuboot size: {}b\n",
          s_.disc_label_block(),
          s_.disc_label_size_in_bytes(),
          s_.romtags_block(),
          s_.romtags_size_in_bytes(),
          newgnuboot_first_block,
          romtag->size);

  ::_get_cross_app_sig(s_,original_sig);
  _vprint("   - original sig: {}\n",original_sig);

  md5_calc(data.data(),
           data.size(),
           digest);

  tdo_rsa_sign(TDO_KEY_APP,digest,computed_sig);
  _vprint("   - computed sig: {}\n",computed_sig);

  const bool matched = (memcmp(original_sig,computed_sig,sizeof(rsa512_sig_t)) == 0);
  _vprint("   - match: {}\n",matched);
  _vprint("   - status: {}\n",_sig_status(original_sig,matched,saw_unsigned_,saw_invalid_));

  return matched;
}

static
bool
_verify_file(TDO::DevStream &s_,
             const u64       start_offset_in_blocks_,
             const u64       size_in_bytes_,
             const char     *key_,
             bool           &saw_unsigned_,
             bool           &saw_invalid_)
{
  md5_digest_t digest;
  rsa512_sig_t original_sig;
  rsa512_sig_t computed_sig;
  std::vector<char> data;

  _vprint("   - start block: {}\n"
          "   - file size: {}b\n",
          start_offset_in_blocks_,
          size_in_bytes_);
  if(size_in_bytes_ < RSA512_SIG_SIZE)
    {
      _vprint("   - error: file is too small to contain a signature\n");
      return false;
    }
  if(!_range_in_image(s_,s_.size_in_bytes(),start_offset_in_blocks_,size_in_bytes_))
    {
      _vprint("   - error: file is outside image bounds\n");
      return false;
    }
  s_.read_data_bytes_from_block(data,
                                start_offset_in_blocks_,
                                size_in_bytes_);

  _get_sig_from_end(data,original_sig);
  _vprint("   - original sig: {}\n",original_sig);

  // CD-ROM ROMTag payload signatures are raw-byte digests up to the
  // trailing signature (cdromdipir.c:ReadOsComponent). Do not apply
  // RSACheck's _3DO_SignatureLen zeroing here.
  md5_calc(data.data(),
           data.size() - RSA512_SIG_SIZE,
           digest);
  tdo_rsa_sign(key_,
               digest,
               computed_sig);

  _vprint("   - computed sig: {}\n",computed_sig);

  const bool matched = (memcmp(original_sig,computed_sig,sizeof(rsa512_sig_t)) == 0);
  _vprint("   - match: {}\n",matched);
  _vprint("   - status: {}\n",_sig_status(original_sig,matched,saw_unsigned_,saw_invalid_));

  return matched;
}

static
bool
_verify_boot_code_post_cheeze(TDO::DevStream    &s_,
                              const TDO::ROMTag &rom_tag_,
                              bool              &saw_unsigned_,
                              bool              &saw_invalid_)
{
  md5_digest_t digest;
  rsa512_sig_t original_sig;
  rsa512_sig_t computed_sig;
  std::vector<char> data;
  const u64 start_offset_in_blocks = TDO::safe_romtag_first_data_block(s_,rom_tag_,"boot_code");
  const u64 size_in_bytes = rom_tag_.size;

  _vprint(" - Verifying NEWKNEWNEWGNUBOOT post-cheeze with 3DO Key:\n");
  if(size_in_bytes < (RSA512_SIG_SIZE * 2))
    {
      _vprint("   - error: boot_code is too small to contain both signatures\n");
      return false;
    }
  if(!_range_in_image(s_,s_.size_in_bytes(),start_offset_in_blocks,size_in_bytes))
    {
      _vprint("   - error: boot_code is outside image bounds\n");
      return false;
    }

  s_.read_data_bytes_from_block(data,
                                start_offset_in_blocks,
                                size_in_bytes - RSA512_SIG_SIZE);
  TDO::decrypt_boot_code_range(data.data(),data.size());

  _vprint("   - decrypted data size: {}b\n",
          data.size() - RSA512_SIG_SIZE);

  _get_sig_from_end(data,original_sig);
  _vprint("   - original sig: {}\n",original_sig);

  md5_calc(data.data(),
           data.size() - RSA512_SIG_SIZE,
           digest);
  tdo_rsa_sign(TDO_KEY_3DO,
               digest,
               computed_sig);

  _vprint("   - computed sig: {}\n",computed_sig);

  const bool matched = (memcmp(original_sig,computed_sig,sizeof(rsa512_sig_t)) == 0);
  _vprint("   - match: {}\n",matched);
  _vprint("   - status: {}\n",_sig_status(original_sig,matched,saw_unsigned_,saw_invalid_));

  return matched;
}

static
bool
_has_checkable_rsa_sig(TDO::DevStream &s_)
{
  TDO::ROMTagVec rom_tags;

  rom_tags = s_.romtags();
  for(const auto &rom_tag : rom_tags)
    {
      switch(rom_tag.type)
        {
        case RSA_OS:
        case RSA_MISCCODE:
        case RSA_NEWKNEWNEWGNUBOOT:
        case RSA_APPSPLASH:
          return true;
        }
    }

  return false;
}

static
bool
_verify_romtag_assets(TDO::DevStream &s_,
                      bool           &saw_unsigned_,
                      bool           &saw_invalid_)
{
  bool matched;
  u64 size_in_bytes;
  u64 offset_in_blocks;
  TDO::ROMTagVec rom_tags;

  matched = true;
  rom_tags = s_.romtags();
  for(const auto &rom_tag : rom_tags)
    {
      // Filter to types we actually verify before computing first_block,
      // so unrelated tags don't trip the wrap check unnecessarily.
      switch(rom_tag.type)
        {
        case RSA_OS:
        case RSA_MISCCODE:
        case RSA_NEWKNEWNEWGNUBOOT:
        case RSA_APPSPLASH:
          break;
        default:
          continue;
        }

      try
        {
          offset_in_blocks = TDO::safe_romtag_first_data_block(s_,rom_tag,rom_tag.type_str().c_str());
          size_in_bytes    = rom_tag.size;

          switch(rom_tag.type)
            {
            case RSA_OS:
            case RSA_MISCCODE:
            case RSA_NEWKNEWNEWGNUBOOT:
              _vprint(" - Verifying {} with 3DO Key:\n",
                      rom_tag.type_str());
              matched &= ::_verify_file(s_,offset_in_blocks,size_in_bytes,TDO_KEY_3DO,
                                        saw_unsigned_,saw_invalid_);
              if(rom_tag.type == RSA_NEWKNEWNEWGNUBOOT)
                matched &= ::_verify_boot_code_post_cheeze(s_,rom_tag,
                                                           saw_unsigned_,saw_invalid_);
              break;
            case RSA_APPSPLASH:
              _vprint(" - Verifying {} with APP Key:\n",
                      rom_tag.type_str());
              matched &= ::_verify_file(s_,offset_in_blocks,size_in_bytes,TDO_KEY_APP,
                                        saw_unsigned_,saw_invalid_);
              break;
            }
        }
      catch(const std::exception &e)
        {
          // A malformed tag (e.g. out-of-range offset caught by
          // safe_romtag_first_data_block) used to be reported via
          // _range_in_image's bool return, which let the loop
          // continue past it. Preserve that behavior so one bad tag
          // does not hide the verification result of every subsequent
          // tag in the same image.
          _vprint("   - error verifying {}: {}\n",
                  rom_tag.type_str(),
                  e.what());
          matched = false;
          saw_invalid_ = true;
        }
    }

  return matched;
}

static
void
_verify_operafs_structure(TDO::DevStream &s_)
{
  VerifyFSCallbacks callbacks;
  TDO::FSWalker fsw(s_,callbacks);

  _vprint(" - Verifying OperaFS structure\n");

  fsw.walk();
}

static
VerifyStatus
_verify_rsa_sigs(TDO::DevStream &s_)
{
  bool matched;
  bool saw_unsigned;
  bool saw_invalid;
  std::optional<TDO::ROMTag> signatures_romtag;

  saw_unsigned = false;
  saw_invalid = false;

  if(!_has_checkable_rsa_sig(s_))
    {
      _vprint(" - No RSA signatures found\n");
      return VerifyStatus::Unsigned;
    }

  // Portfolio's no-banner APPDIGEST path requires this ROMTag even when its
  // TypeSpecific digest count is zero. 3dt deliberately does not implement
  // the optional block-digest table, so only the required tag is checked.
  // Check it only after establishing that the image has signed components:
  // an image with no checkable RSA ROMTags is unsigned, not malformed.
  signatures_romtag = s_.romtag(RSA_SIGNATURE_BLOCK);
  if(!signatures_romtag)
    {
      _vprint(" - Missing required RSA_SIGNATURE_BLOCK ROMTag\n");
      return VerifyStatus::Invalid;
    }
  _vprint(" - RSA_SIGNATURE_BLOCK ROMTag present"
          " (TypeSpecific: {}; digest payload not checked)\n",
          signatures_romtag->type_specific);

  matched = true;
  try
    {
      matched = _verify_disclabel_romtags_bootcode(s_,saw_unsigned,saw_invalid);
    }
  catch(const std::exception &e)
    {
      // Match the per-tag isolation _verify_romtag_assets uses: a
      // malformed NEWKNEWNEWGNUBOOT (e.g. offset out of range caught
      // by safe_romtag_first_data_block) or a failed read should
      // record an invalid signature outcome and let the per-tag asset
      // verification still run, rather than escaping to the outer
      // _verify catch and skipping every later tag in the image.
      _vprint(" - error verifying DiscLabel + ROMTags + BootCode: {}\n",e.what());
      matched = false;
      saw_invalid = true;
    }
  matched &= _verify_romtag_assets(s_,saw_unsigned,saw_invalid);
  try
    {
      if(!_verify_romtag_metadata(s_))
        {
          matched = false;
          saw_invalid = true;
        }
    }
  catch(const std::exception &e)
    {
      _vprint(" - error verifying ROMTag metadata: {}\n",e.what());
      matched = false;
      saw_invalid = true;
    }

  if(matched)
    return VerifyStatus::Valid;
  if(saw_invalid)
    return VerifyStatus::Invalid;
  if(saw_unsigned)
    return VerifyStatus::Unsigned;

  return VerifyStatus::Invalid;
}

static
void
_verify(const Options::Verify &opts_);

static
void
_print_summary(const std::string                              &format_,
               const bool                                      quiet_,
               const std::vector<VerifyResult>                &results_)
{
  if(format_ == "csv")
    {
      fmt::print("status,path\n");
      for(const auto &result : results_)
        fmt::print("{},{}\n",verify_status_str(result.status),result.path);
      return;
    }

  if(format_ == "json")
    {
      nlohmann::json arr = nlohmann::json::array();
      for(const auto &result : results_)
        arr.push_back({{"path", result.path}, {"status", verify_status_str(result.status)}});
      fmt::print("{}\n", arr.dump());
      return;
    }

  if(quiet_)
    for(const auto &result : results_)
      fmt::print("{}: {}\n",result.path,verify_status_str(result.status));
}

void
Subcmd::verify(const Options::Verify &opts_)
{
  ::_verify(opts_);
}

static
void
_verify(const Options::Verify &opts_)
{
  bool failed;
  int exit_code;
  std::string format;
  std::vector<VerifyResult> results;

  format = opts_.format.empty() ? "human" : opts_.format;

  g_quiet = (opts_.quiet || (format != "human"));
  failed = false;
  exit_code = 0;
  for(const auto &filepath : opts_.filepaths)
    {
      bool file_failed;
      VerifyStatus status;
      TDO::FileStream stream;

      file_failed = false;
      status = VerifyStatus::Valid;
      try
        {
          stream.open(filepath);

          if(!stream.has_romtags())
            {
              fmt::print(stderr,"3dt: {} does not contain ROMTags\n",filepath);
              status = VerifyStatus::Unsupported;
              file_failed = true;
              throw Error("image does not contain ROMTags");
            }

          _vprint("{}:\n",filepath);
          ::_verify_operafs_structure(stream);
          status = ::_verify_rsa_sigs(stream);
          if(status != VerifyStatus::Valid)
            file_failed = true;
        }
      catch(const std::exception &e)
        {
          if(!file_failed)
            fmt::print(stderr,"3dt: {} - {}\n",e.what(),filepath);
          if(status == VerifyStatus::Valid)
            status = VerifyStatus::Invalid;
          file_failed = true;
        }

      if(file_failed)
        {
          failed = true;
          if(status == VerifyStatus::Invalid)
            exit_code = VERIFY_EXIT_INVALID;
          else if((status == VerifyStatus::Unsupported) &&
                  (exit_code != VERIFY_EXIT_INVALID))
            exit_code = VERIFY_EXIT_UNSUPPORTED;
          else if((status == VerifyStatus::Unsigned) &&
                  (exit_code == 0))
            exit_code = VERIFY_EXIT_UNSIGNED;
        }
      results.push_back({filepath.generic_string(),status});
    }

  _print_summary(format,opts_.quiet,results);
  if(failed)
    {
      if(exit_code == VERIFY_EXIT_UNSIGNED)
        throw Error("image is unsigned",exit_code);
      if(exit_code == VERIFY_EXIT_UNSUPPORTED)
        throw Error("unsupported image",exit_code);
      throw Error("verification failed",exit_code);
    }
}
