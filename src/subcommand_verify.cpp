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

#include "types_ints.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

static constexpr u64 PHY_BLOCK_SIZE = 2048;
static constexpr u64 LOG_BLOCK_SIZE = 32768;
static constexpr u64 LOG_BLOCKS_PER_DIGEST = (LOG_BLOCK_SIZE / PHY_BLOCK_SIZE);
static constexpr int VERIFY_EXIT_UNSIGNED = 2;
static constexpr int VERIFY_EXIT_UNSUPPORTED = 3;
static constexpr int VERIFY_EXIT_INVALID = 4;

static bool g_check_digest_table = true;
static bool g_quiet = false;
static bool g_saw_unsigned_sig = false;
static bool g_saw_invalid_sig = false;

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
u64
_div_round_up(const u64 number_,
              const u64 multiple_)
{
  return ((number_ + multiple_ - 1) / multiple_);
}

static
bool
_range_in_image(TDO::DevStream &s_,
                const u64       start_block_,
                const u64       byte_count_)
{
  s64 file_offset;
  s64 image_size;

  if(byte_count_ == 0)
    return true;

  file_offset = s_.data_block_to_file_offset(start_block_);
  image_size = s_.size_in_bytes();
  if((file_offset < 0) || (image_size < 0))
    return false;
  if(static_cast<u64>(file_offset) > static_cast<u64>(image_size))
    return false;

  return (byte_count_ <= (static_cast<u64>(image_size) - static_cast<u64>(file_offset)));
}

static
std::string
_json_escape(const std::string &str_)
{
  std::string out;

  for(const char c : str_)
    {
      switch(c)
        {
        case '\\':
          out += "\\\\";
          break;
        case '"':
          out += "\\\"";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          out += c;
          break;
        }
    }

  return out;
}

static
std::string
_display_invalid_path(const std::filesystem::path &parent_,
                      const std::string           &filename_)
{
  std::string path;

  path = parent_.generic_string();
  if(!path.empty() && !filename_.empty() && (filename_[0] != '/'))
    path += "/";
  if(filename_.empty())
    path += "<invalid empty filename>";
  else
    path += filename_;

  return path;
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
               _display_invalid_path(parent_,filename_));

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
  std::copy((data_.end() - sizeof(rsa512_sig_t)),
            data_.end(),
            sig_);
}

static
bool
_is_zero_sig(const rsa512_sig_t sig_)
{
  for(u64 i = 0; i < sizeof(rsa512_sig_t); i++)
    if(sig_[i] != 0)
      return false;

  return true;
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
            const bool         matched_)
{
  if(matched_)
    return "valid";
  if(_is_zero_sig(sig_))
    {
      g_saw_unsigned_sig = true;
      return "unsigned placeholder: zero";
    }
  if(_is_iamaduck_sig(sig_))
    {
      g_saw_unsigned_sig = true;
      return "unsigned placeholder: iamaduck";
    }

  g_saw_invalid_sig = true;
  return "invalid";
}


// See details in portfolio_os/dipir/cdipir.c:1178
static
bool
_verify_disclabel_romtags_bootcode(TDO::DevStream &s_)
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
  if(!_range_in_image(s_,romtag->offset + 1,romtag->size))
    {
      _vprint("   - error: NEWKNEWNEWGNUBOOT is outside image bounds\n");
      return false;
    }
    
  s_.read_data_bytes_from_block(data,
                                romtag->offset + 1,
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
          romtag->offset + 1,
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
  _vprint("   - status: {}\n",_sig_status(original_sig,matched));

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
    return ((app_romtag->offset + s_.romtags_block()) / LOG_BLOCKS_PER_DIGEST);
  if(num_digests_ <= (256 / LOG_BLOCKS_PER_DIGEST))
    return 0;

  return (256 / LOG_BLOCKS_PER_DIGEST);
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
  const u64 digest_start_byte = (digest_ * LOG_BLOCK_SIZE);
  const u64 digest_end_byte = (digest_start_byte + LOG_BLOCK_SIZE);

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
  const u64 start_byte = (avatar_ * PHY_BLOCK_SIZE);
  const u64 first_digest = (start_byte / LOG_BLOCK_SIZE);
  const u64 last_digest = ((start_byte + byte_count_ - 1) / LOG_BLOCK_SIZE);

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
Error
_collect_valid_digests(TDO::DevStream    &s_,
                       const u64          num_digests_,
                       std::vector<bool> &valid_digests_)
{
  ValidDigestCollector callbacks(valid_digests_);
  TDO::FSWalker fsw(s_,callbacks);

  valid_digests_.assign(num_digests_,false);

  return fsw.walk();
}

static
bool
_signature_digest_is_mutable(const u64 digest_,
                             const u64 signature_block_,
                             const u64 signature_size_)
{
  const u64 signature_start_byte = (signature_block_ * PHY_BLOCK_SIZE);
  const u64 signature_end_byte = (signature_start_byte + signature_size_);
  const u64 digest_start_byte = (digest_ * LOG_BLOCK_SIZE);
  const u64 digest_end_byte = (digest_start_byte + LOG_BLOCK_SIZE);

  return ((digest_start_byte < signature_end_byte) &&
          (digest_end_byte > signature_start_byte));
}

static
bool
_verify_signature_digests(TDO::DevStream          &s_,
                          const std::vector<char> &signatures_,
                          const u64                num_digests_,
                          const u64                signature_block_,
                          const u64                signature_size_)
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

  Error err = _collect_valid_digests(s_,num_digests_,valid_digests);
  if(err)
    {
      _vprint("   - digest table comparison: false\n"
              "   - digest table error: {}\n",
              err.str);
      return false;
    }

  digest_start32 = _portfolio_digest_start32(s_,num_digests_);
  checked_count = 0;
  skipped_ignored_count = 0;
  skipped_prefix_count = 0;
  skipped_mutable_count = 0;
  skipped_unallocated_count = 0;
  for(u64 i = 0; i < num_digests_; i++)
    {
      const u64 expected_offset = (i * sizeof(md5_digest_t));
      const u64 block_pos = (i * LOG_BLOCKS_PER_DIGEST);

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
      s_.read_data_blocks(data,block_pos,LOG_BLOCKS_PER_DIGEST);
      md5_calc(data.data(),data.size(),digest);

      if(memcmp(&signatures_[expected_offset],digest,sizeof(md5_digest_t)) != 0)
        {
          _vprint("   - digest table comparison: false\n"
                  "   - digest mismatch index: {}\n"
                  "   - digest mismatch block: {}\n",
                  i,
                  block_pos);
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
                       const TDO::ROMTag &rom_tag_)
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
  TDO::DiscLabel disc_label;

  disc_label = s_.disc_label();

  // This setup comes from Portfolio OS dipir/appdigest.c. When the digest
  // count is divisible by 512, appdigest reads an extra 8192-byte trailer;
  // the RSA_SIGNATURE_BLOCK ROMTag may still report only digest bytes.
  volume_block_count = disc_label.volume_block_count;
  sigfile_block_start = rom_tag_.offset + 1;
  sigfile_size = rom_tag_.size;
  num_digests = (disc_label.volume_block_count * PHY_BLOCK_SIZE) / LOG_BLOCK_SIZE;
  const u64 digest_table_size = num_digests * sizeof(md5_digest_t);
  if(((num_digests & 511) == 0) && (sigfile_size == digest_table_size))
    sigfile_size += 8192;
  sigfile_block_count = _div_round_up(sigfile_size,PHY_BLOCK_SIZE);
  sigfile_block_end = sigfile_block_start + sigfile_block_count;

  if(sigfile_size < RSA512_SIG_SIZE)
    {
      _vprint("   - error: signature file is too small\n");
      return false;
    }
  if(!_range_in_image(s_,sigfile_block_start,sigfile_size))
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
  _vprint("   - status: {}\n",_sig_status(original_sig,matched));

  if(!g_check_digest_table)
    {
      _vprint("   - digest table comparison: skipped\n");
      return matched;
    }

  digests_matched = _verify_signature_digests(s_,
                                              signatures,
                                              num_digests,
                                              sigfile_block_start,
                                              rom_tag_.size);
  return (matched && digests_matched);
}

static
bool
_verify_file(TDO::DevStream &s_,
             const u64       start_offset_in_blocks_,
             const u64       size_in_bytes_,
             const char     *key_)
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
  if(!_range_in_image(s_,start_offset_in_blocks_,size_in_bytes_))
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
  _vprint("   - status: {}\n",_sig_status(original_sig,matched));

  return matched;
}

static
bool
_verify_boot_code_post_cheeze(TDO::DevStream    &s_,
                              const TDO::ROMTag &rom_tag_)
{
  md5_digest_t digest;
  rsa512_sig_t original_sig;
  rsa512_sig_t computed_sig;
  std::vector<char> data;
  const u64 start_offset_in_blocks = rom_tag_.offset + 1;
  const u64 size_in_bytes = rom_tag_.size;

  _vprint(" - Verifying NEWKNEWNEWGNUBOOT post-cheeze with 3DO Key:\n");
  if(size_in_bytes < (RSA512_SIG_SIZE * 2))
    {
      _vprint("   - error: boot_code is too small to contain both signatures\n");
      return false;
    }
  if(!_range_in_image(s_,start_offset_in_blocks,size_in_bytes))
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
  _vprint("   - status: {}\n",_sig_status(original_sig,matched));

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
_verify_romtag_assets(TDO::DevStream &s_)
{
  bool matched;
  int size_in_bytes;  
  int offset_in_blocks;
  TDO::ROMTagVec rom_tags;

  matched = true;
  rom_tags = s_.romtags();
  for(const auto &rom_tag : rom_tags)
    {
      offset_in_blocks = rom_tag.offset + 1;
      size_in_bytes    = rom_tag.size;

      switch(rom_tag.type)
        {
        case RSA_OS:
          _vprint(" - Verifying {} with 3DO Key:\n",
                  rom_tag.type_str());
          matched &= ::_verify_file(s_,offset_in_blocks,size_in_bytes,TDO_KEY_3DO);
          break;
        case RSA_MISCCODE:
          _vprint(" - Verifying {} with 3DO Key:\n",
                  rom_tag.type_str());
          matched &= ::_verify_file(s_,offset_in_blocks,size_in_bytes,TDO_KEY_3DO);
          break;
        case RSA_NEWKNEWNEWGNUBOOT:
          _vprint(" - Verifying {} with 3DO Key:\n",
                  rom_tag.type_str());
          matched &= ::_verify_file(s_,offset_in_blocks,size_in_bytes,TDO_KEY_3DO);
          matched &= ::_verify_boot_code_post_cheeze(s_,rom_tag);
          break;
        case RSA_APPSPLASH:
          _vprint(" - Verifying {} with APP Key:\n",
                  rom_tag.type_str());
          matched &= ::_verify_file(s_,offset_in_blocks,size_in_bytes,TDO_KEY_APP);
          break;
        case RSA_SIGNATURE_BLOCK:          
          _vprint(" - Verifying {} with APP Key:\n",
                  rom_tag.type_str());
          matched &= ::_verify_signature_file(s_,rom_tag);
          break;
        }
    }

  return matched;
}

static
Error
_verify_operafs_structure(TDO::DevStream &s_)
{
  VerifyFSCallbacks callbacks;
  TDO::FSWalker fsw(s_,callbacks);

  _vprint(" - Verifying OperaFS structure\n");

  return fsw.walk();
}

static
VerifyStatus
_verify_rsa_sigs(TDO::DevStream &s_)
{
  bool matched;

  if(!_has_checkable_rsa_sig(s_))
    {
      _vprint(" - No RSA signatures found\n");
      return VerifyStatus::Unsigned;
    }

  matched = _verify_disclabel_romtags_bootcode(s_);
  matched &= _verify_romtag_assets(s_);

  if(matched)
    return VerifyStatus::Valid;
  if(g_saw_invalid_sig)
    return VerifyStatus::Invalid;
  if(g_saw_unsigned_sig)
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
      fmt::print("[");
      for(std::size_t i = 0; i < results_.size(); i++)
        {
          if(i > 0)
            fmt::print(",");
          fmt::print("{{\"path\":\"{}\",\"status\":\"{}\"}}",
                     _json_escape(results_[i].path),
                     verify_status_str(results_[i].status));
        }
      fmt::print("]\n");
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
      Error err;
      bool file_failed;
      VerifyStatus status;
      TDO::FileStream stream;

      file_failed = false;
      status = VerifyStatus::Valid;
      g_saw_unsigned_sig = false;
      g_saw_invalid_sig = false;
      try
        {
          err = stream.open(filepath);
          if(err)
            {
              fmt::print(stderr,"3dt: {} - {}\n",err.str,filepath);
              status = VerifyStatus::Unsupported;
              file_failed = true;
              throw std::runtime_error(err.str);
            }

          if(!stream.has_romtags())
            {
              fmt::print(stderr,"3dt: {} does not contain ROMTags\n",filepath);
              status = VerifyStatus::Unsupported;
              file_failed = true;
              throw std::runtime_error("image does not contain ROMTags");
            }

          _vprint("{}:\n",filepath);
          err = ::_verify_operafs_structure(stream);
          if(err)
            {
              fmt::print(stderr,"3dt: {} - {}\n",err.str,filepath);
              status = VerifyStatus::Invalid;
              file_failed = true;
              throw std::runtime_error(err.str);
            }
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
