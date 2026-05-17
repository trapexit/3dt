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
#include "subcommand.hpp"

#include "options.hpp"
#include "tdo_dev_stream.hpp"
#include "tdo_disc_format.hpp"
#include "tdo_disc_label.hpp"
#include "tdo_romtag.hpp"
#include "tdo_boot_code_crypto.hpp"
#include "tdo_file_stream.hpp"
#include "tdo_fs_walker.hpp"
#include "tdo_rsa.h"

#include "discdata.h"

#include "fmt.hpp"
#include "fmt_md5_digest.hpp"
#include "fmt_rsa512_sig.hpp"
#include "json.hpp"

#include "types_ints.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

static constexpr int VERIFY_EXIT_UNSIGNED = 2;
static constexpr int VERIFY_EXIT_UNSUPPORTED = 3;
static constexpr int VERIFY_EXIT_INVALID = 4;

static bool g_check_digest_table = true;
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
u64
_portfolio_digest_start32(TDO::DevStream &s_,
                          const u64       num_digests_)
{
  std::optional<TDO::ROMTag> app_romtag;

  app_romtag = s_.romtag(RSA_APP);
  if(app_romtag)
    return ((app_romtag->offset + s_.romtags_block()) / TDO::LOG_BLOCKS_PER_DIGEST);
  if(num_digests_ <= (256 / TDO::LOG_BLOCKS_PER_DIGEST))
    return 0;

  return (256 / TDO::LOG_BLOCKS_PER_DIGEST);
}

static
bool
_portfolio_digest_is_ignored(const u64 digest_)
{
  return ((digest_ == 0x080F) ||
          (digest_ == 0x0306));
}

static
bool
_digest_overlaps_extent(const u64 digest_,
                        const u64 start_byte_,
                        const u64 byte_count_)
{
  const u64 extent_end_byte = (start_byte_ + byte_count_);
  const u64 digest_start_byte = (digest_ * TDO::LOG_BLOCK_SIZE);
  const u64 digest_end_byte = (digest_start_byte + TDO::LOG_BLOCK_SIZE);

  if(byte_count_ == 0)
    return false;

  return ((digest_start_byte < extent_end_byte) &&
          (digest_end_byte > start_byte_));
}

static
void
_mark_valid_digest_extent(std::vector<bool> &valid_digests_,
                          const u64          avatar_,
                          const u64          byte_count_)
{
  const u64 start_byte = (avatar_ * TDO::BLOCK_SIZE);
  const u64 first_digest = (start_byte / TDO::LOG_BLOCK_SIZE);
  const u64 last_digest = ((start_byte + byte_count_ - 1) / TDO::LOG_BLOCK_SIZE);

  if(byte_count_ == 0)
    return;

  for(u64 digest = first_digest;
      (digest <= last_digest) && (digest < valid_digests_.size());
      digest++)
    if(_digest_overlaps_extent(digest,start_byte,byte_count_))
      valid_digests_[digest] = true;
}

static
void
_mark_valid_record_digests(std::vector<bool>              &valid_digests_,
                           const TDO::DirectoryRecord     &record_)
{
  for(const auto avatar : record_.avatar_list)
    _mark_valid_digest_extent(valid_digests_,avatar,record_.byte_count);
}

class ValidDigestCollector final : public TDO::FSWalker::Callbacks
{
public:
  ValidDigestCollector(std::vector<bool> &valid_digests_)
    : _valid_digests(valid_digests_)
  {
  }

public:
  void
  operator()(const std::filesystem::path&,
             const TDO::DirectoryRecord  &record_,
             const uint32_t,
             TDO::DevStream&)
  {
    _mark_valid_record_digests(_valid_digests,record_);
  }

  Error
  invalid_filename(const std::filesystem::path&,
                   const std::string&,
                   const TDO::DirectoryRecord &record_,
                   const uint32_t,
                   const Error&,
                   TDO::DevStream&)
  {
    _mark_valid_record_digests(_valid_digests,record_);

    return {};
  }

private:
  std::vector<bool> &_valid_digests;
};

static
void
_collect_valid_digests(TDO::DevStream    &s_,
                        const u64          num_digests_,
                        std::vector<bool> &valid_digests_)
{
  ValidDigestCollector callbacks(valid_digests_);
  TDO::FSWalker fsw(s_,callbacks);

  valid_digests_.assign(num_digests_,false);

  fsw.walk();
}


// --- Digest-to-file mapping for mismatch reporting ---

static
void
_mark_digest_file_overlap(std::vector<std::vector<std::string>> &digest_to_files_,
                            const u64                             digest_,
                            const std::string                   &filepath_)
{
  if(digest_ < digest_to_files_.size())
    digest_to_files_[digest_].push_back(filepath_);
}

static
void
_mark_digest_extent_files(std::vector<std::vector<std::string>> &digest_to_files_,
                            const u64                              avatar_,
                            const u64                              byte_count_,
                            const std::string                    &filepath_)
{
  if(byte_count_ == 0)
    return;

  const u64 start_byte = (avatar_ * TDO::BLOCK_SIZE);
  const u64 first_digest = (start_byte / TDO::LOG_BLOCK_SIZE);
  const u64 last_digest = ((start_byte + byte_count_ - 1) / TDO::LOG_BLOCK_SIZE);

  for(u64 digest = first_digest;
      (digest <= last_digest) && (digest < digest_to_files_.size());
      digest++)
    {
      if(_digest_overlaps_extent(digest,start_byte,byte_count_))
        _mark_digest_file_overlap(digest_to_files_,digest,filepath_);
    }
}

static
void
_mark_record_digest_files(std::vector<std::vector<std::string>> &digest_to_files_,
                            const TDO::DirectoryRecord          &record_,
                            const std::filesystem::path           &path_)
{
  for(const auto avatar : record_.avatar_list)
    _mark_digest_extent_files(digest_to_files_,avatar,record_.byte_count,
                              path_.generic_string());
}

class FileDigestCollector final : public TDO::FSWalker::Callbacks
{
public:
  FileDigestCollector(std::vector<std::vector<std::string>> &digest_to_files_)
    : _digest_to_files(digest_to_files_)
  {
  }

public:
  void
  operator()(const std::filesystem::path    &path_,
             const TDO::DirectoryRecord     &record_,
             const uint32_t,
             TDO::DevStream&)
  {
    _mark_record_digest_files(_digest_to_files,record_,path_);
  }

  Error
  invalid_filename(const std::filesystem::path    &parent_,
                   const std::string             &filename_,
                   const TDO::DirectoryRecord     &record_,
                   const uint32_t,
                   const Error&,
                   TDO::DevStream&)
  {
    const std::filesystem::path path = TDO::display_path(parent_,filename_);
    _mark_record_digest_files(_digest_to_files,record_,path);

    return {};
  }

private:
  std::vector<std::vector<std::string>> &_digest_to_files;
};

static
void
_collect_digest_files(TDO::DevStream                       &s_,
                        const u64                             num_digests_,
                        std::vector<std::vector<std::string>> &digest_to_files_)
{
  FileDigestCollector callbacks(digest_to_files_);
  TDO::FSWalker fsw(s_,callbacks);

  digest_to_files_.assign(num_digests_,{});

  fsw.walk();
}

static
bool
_signature_digest_is_mutable(const u64 digest_,
                             const u64 signature_block_,
                             const u64 signature_size_)
{
  const u64 signature_start_byte = (signature_block_ * TDO::BLOCK_SIZE);
  const u64 signature_end_byte = (signature_start_byte + signature_size_);
  const u64 digest_start_byte = (digest_ * TDO::LOG_BLOCK_SIZE);
  const u64 digest_end_byte = (digest_start_byte + TDO::LOG_BLOCK_SIZE);

  return ((digest_start_byte < signature_end_byte) &&
          (digest_end_byte > signature_start_byte));
}

static
void
_print_affected_files(const u64                                        digest_,
                      const u64                                        block_pos_,
                      const std::vector<std::vector<std::string>>     &digest_to_files_)
{
  const u64 end_block = block_pos_ + TDO::LOG_BLOCKS_PER_DIGEST - 1;

  _vprint("   - digest mismatch covers blocks: {}-{} ({:#010x}-{:#010x})\n",
          block_pos_,end_block,
          block_pos_,end_block);
  _vprint("   - affected files/data:\n");

  if(digest_ < digest_to_files_.size() && !digest_to_files_[digest_].empty())
    {
      std::vector<std::string> seen;
      for(const auto &path : digest_to_files_[digest_])
        {
          if(std::find(seen.begin(),seen.end(),path) == seen.end())
            {
              seen.push_back(path);
              _vprint("     - {}\n",path);
            }
        }
    }
  else
    {
      _vprint("     - (unallocated space)\n");
    }
}

static
bool
_verify_signature_digests(TDO::DevStream                       &s_,
                          const std::vector<char>              &signatures_,
                          const u64                             num_digests_,
                          const u64                             signature_block_,
                          const u64                             signature_size_,
                          const std::vector<std::vector<std::string>> &digest_to_files_)
{
  u64 checked_count;
  u64 digest_start32;
  u64 skipped_ignored_count;
  u64 skipped_mutable_count;
  u64 skipped_prefix_count;
  u64 skipped_unallocated_count;
  md5_digest_t digest;
  std::vector<char> data;
  std::vector<bool> valid_digests;

  if(signatures_.size() < (num_digests_ * sizeof(md5_digest_t)))
    {
      _vprint("   - digest table comparison: false\n"
              "   - digest table error: too small\n");
      return false;
    }

  _collect_valid_digests(s_,num_digests_,valid_digests);

  digest_start32 = _portfolio_digest_start32(s_,num_digests_);
  checked_count = 0;
  skipped_ignored_count = 0;
  skipped_prefix_count = 0;
  skipped_mutable_count = 0;
  skipped_unallocated_count = 0;
  for(u64 i = 0; i < num_digests_; i++)
    {
      const u64 expected_offset = (i * sizeof(md5_digest_t));
      const u64 block_pos = (i * TDO::LOG_BLOCKS_PER_DIGEST);

      if(i < digest_start32)
        {
          skipped_prefix_count++;
          continue;
        }

      if(!valid_digests[i])
        {
          skipped_unallocated_count++;
          continue;
        }

      if(_portfolio_digest_is_ignored(i))
        {
          skipped_ignored_count++;
          continue;
        }

      if(_signature_digest_is_mutable(i,signature_block_,signature_size_))
        {
          skipped_mutable_count++;
          continue;
        }

      data.clear();
      s_.read_data_blocks(data,block_pos,TDO::LOG_BLOCKS_PER_DIGEST);
      md5_calc(data.data(),data.size(),digest);

      if(memcmp(&signatures_[expected_offset],digest,sizeof(md5_digest_t)) != 0)
        {
          _vprint("   - digest table comparison: false\n"
                  "   - digest mismatch index: {}\n"
                  "   - digest mismatch block: {}\n",
                  i,
                  block_pos);
          _print_affected_files(i,block_pos,digest_to_files_);
          return false;
        }

      checked_count++;
    }

  _vprint("   - digest table comparison: true\n"
          "   - digest table policy: Portfolio max-check exhaustive\n"
          "   - digest table start index: {}\n"
          "   - digest table checked: {}\n"
          "   - digest table skipped prefix: {}\n"
          "   - digest table skipped ignored: {}\n"
          "   - digest table skipped unallocated: {}\n"
          "   - digest table skipped mutable: {}\n",
          digest_start32,
          checked_count,
          skipped_prefix_count,
          skipped_ignored_count,
          skipped_unallocated_count,
          skipped_mutable_count);

  return true;
}

static
bool
_verify_signature_file(TDO::DevStream    &s_,
                       const TDO::ROMTag &rom_tag_,
                       bool              &saw_unsigned_,
                       bool              &saw_invalid_)
{
  u64 sigfile_size = 0;
  u64 num_digests = 0;
  u64 sigfile_block_start = 0;
  u64 sigfile_block_end = 0;
  u64 sigfile_block_count = 0;
  u64 volume_block_count = 0;
  bool digests_matched;
  md5_digest_t digest;
  rsa512_sig_t original_sig;
  rsa512_sig_t computed_sig;
  std::vector<char> signatures;
  std::vector<std::vector<std::string>> digest_to_files;
  TDO::DiscLabel disc_label;

  disc_label = s_.disc_label();
  const s64 image_size = s_.size_in_bytes();

  // This setup comes from Portfolio OS dipir/appdigest.c. When the digest
  // count is divisible by 512, appdigest reads an extra 8192-byte trailer;
  // the RSA_SIGNATURE_BLOCK ROMTag may still report only digest bytes.
  volume_block_count = disc_label.volume_block_count;
  sigfile_block_start = TDO::safe_romtag_first_data_block(s_,rom_tag_,"signatures");
  sigfile_size = rom_tag_.size;
  // volume_block_count is attacker-influenced disc-label data and
  // feeds an unbounded multiplication and downstream allocation
  // (valid_digests.assign(num_digests, ...)). Retail images sometimes
  // had odd-but-valid layouts so we do not bail out, but we must keep
  // the math finite. Detect overflow and out-of-image counts, warn,
  // and clamp num_digests to what the image can physically describe.
  {
    const u64 max_vbc = (std::numeric_limits<u64>::max() / TDO::BLOCK_SIZE);
    u64 effective_vbc = volume_block_count;
    if(effective_vbc > max_vbc)
      {
        _vprint("   - warning: volume_block_count overflows digest math; clamping\n");
        effective_vbc = max_vbc;
      }
    const u64 device_blocks = s_.device_block_count();
    if((device_blocks > 0) && (effective_vbc > device_blocks))
      {
        _vprint("   - warning: volume_block_count exceeds image size; clamping to image\n");
        effective_vbc = device_blocks;
      }
    num_digests = (effective_vbc * TDO::BLOCK_SIZE) / TDO::LOG_BLOCK_SIZE;
  }
  const u64 digest_table_size = num_digests * sizeof(md5_digest_t);
  if(((num_digests & 511) == 0) && (sigfile_size == digest_table_size))
    sigfile_size += 8192;
  sigfile_block_count = TDO::div_round_up(sigfile_size,TDO::BLOCK_SIZE);
  sigfile_block_end = sigfile_block_start + sigfile_block_count;

  if(sigfile_size < RSA512_SIG_SIZE)
    {
      _vprint("   - error: signature file is too small\n");
      return false;
    }
  if(!_range_in_image(s_,image_size,sigfile_block_start,sigfile_size))
    {
      _vprint("   - error: signature file is outside image bounds\n");
      return false;
    }

  s_.read_data_bytes_from_block(signatures,
                                sigfile_block_start,
                                sigfile_size);

  if(signatures.size() < RSA512_SIG_SIZE)
    {
      _vprint("   - error: signature file is too small\n");
      return false;
    }

  _vprint("   - start block: {}\n"
          "   - start byte: {}\n"
          "   - end block: {}\n"
          "   - end byte: {}\n"
          "   - block count: {}\n"
          "   - file size: {}b\n"
          "   - num digests: {}\n"
          "   - volume block count: {}\n"
          "   - digest table mode: {}\n",
          sigfile_block_start,
          sigfile_block_start * s_.device_block_data_size(),
          sigfile_block_end,
          sigfile_block_end * s_.device_block_data_size(),
          sigfile_block_count,
          sigfile_size,
          num_digests,
          volume_block_count,
          (g_check_digest_table ? "Portfolio" : "skipped"));

  _get_sig_from_end(signatures,original_sig);
  _vprint("   - original sig: {}\n",original_sig);

  md5_calc(signatures.data(),
           signatures.size() - RSA512_SIG_SIZE,
           digest);
  tdo_rsa_sign(TDO_KEY_APP,
               digest,
               computed_sig);

  _vprint("   - computed sig: {}\n",computed_sig);

  const bool matched = (memcmp(original_sig,computed_sig,sizeof(rsa512_sig_t)) == 0);
  _vprint("   - match: {}\n",matched);
  _vprint("   - status: {}\n",_sig_status(original_sig,matched,saw_unsigned_,saw_invalid_));

  if(!g_check_digest_table)
    {
      _vprint("   - digest table comparison: skipped\n");
      return matched;
    }

  _collect_digest_files(s_,num_digests,digest_to_files);

  digests_matched = _verify_signature_digests(s_,
                                              signatures,
                                              num_digests,
                                              sigfile_block_start,
                                              rom_tag_.size,
                                              digest_to_files);
  return (matched && digests_matched);
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
        case RSA_SIGNATURE_BLOCK:
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
        case RSA_SIGNATURE_BLOCK:
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
            case RSA_SIGNATURE_BLOCK:
              _vprint(" - Verifying {} with APP Key:\n",
                      rom_tag.type_str());
              matched &= ::_verify_signature_file(s_,rom_tag,
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

  saw_unsigned = false;
  saw_invalid = false;

  if(!_has_checkable_rsa_sig(s_))
    {
      _vprint(" - No RSA signatures found\n");
      return VerifyStatus::Unsigned;
    }

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
Subcommand::verify(const Options::Verify &opts_)
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

  g_check_digest_table = opts_.digest_table;
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
      results.push_back({filepath.string(),status});
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
