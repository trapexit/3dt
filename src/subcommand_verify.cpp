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
#include "tdo_file_stream.hpp"
#include "tdo_rsa.h"

#include "discdata.h"

#include "fmt.hpp"
#include "fmt_rsa512_sig.hpp"

#include "types_ints.h"

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


// See details in portfolio_os/dipir/cdipir.c:1178
static
void
_verify_disclabel_romtags_bootcode(TDO::DevStream &s_)
{
  md5_digest_t digest;  
  rsa512_sig_t original_sig;
  rsa512_sig_t computed_sig;
  std::vector<char> data;
  std::optional<TDO::ROMTag> romtag;

  fmt::print(" - Verifying DiscLabel + ROMTags + BootCode with APP Key\n");
  
  s_.read_data_bytes_from_block(data,
                                 s_.disc_label_block(),
                                 s_.disc_label_size_in_bytes());
  s_.read_data_bytes_from_block(data,
                                 s_.romtags_block(),
                                 s_.romtags_size_in_bytes());

  romtag = s_.romtag(RSA_NEWKNEWNEWGNUBOOT);
  if(!romtag)
    {
      fmt::print("- No NEWKNEWNEWGNUBOOT romtag found.");
      return;
    }
    
  s_.read_data_bytes_from_block(data,
                                romtag->offset + 1,
                                romtag->size);

  fmt::print("   - disc label block: {}\n"
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
  fmt::print("   - original sig: {}\n",original_sig);
  
  md5_calc(data.data(),
           data.size(),
           digest);

  tdo_rsa_sign(TDO_KEY_APP,digest,computed_sig);
  fmt::print("   - computed sig: {}\n",computed_sig);

  fmt::print("   - match: {}\n",
             (memcmp(original_sig,computed_sig,sizeof(rsa512_sig_t)) == 0));
}

static
void
_verify_signature_file(TDO::DevStream    &s_,
                       const TDO::ROMTag &rom_tag_)
{
  int sigfile_size = 0;
  int num_digests = 0;
  int sigfile_block_start = 0;
  int sigfile_block_end = 0;
  int sigfile_block_count = 0;
  int volume_block_count = 0;
  md5_digest_t digest;
  rsa512_sig_t original_sig;
  rsa512_sig_t computed_sig;
  std::vector<char> signatures;
  TDO::DiscLabel disc_label;

  disc_label = s_.disc_label();

  // This setup can be found in appdigest.c in Portfolio.
  volume_block_count = disc_label.volume_block_count;
  sigfile_block_start = rom_tag_.offset + 1;
  sigfile_size = rom_tag_.size;
  num_digests = (disc_label.volume_block_count * 2048) / 32768;
  if((num_digests & 511) == 0)
    sigfile_size += 8192;
  sigfile_block_end = (sigfile_block_start + (sigfile_size / 2047));
  sigfile_block_count = sigfile_block_end - sigfile_block_start;

  s_.read_data_bytes_from_block(signatures,
                                 sigfile_block_start,
                                 sigfile_size);

  fmt::print("   - start block: {}\n"
             "   - start byte: {}\n"
             "   - end block: {}\n"
             "   - end byte: {}\n"
             "   - block count: {}\n"
             "   - file size: {}b\n"
             "   - num digests: {}\n"
             "   - volume block count: {}\n",
             sigfile_block_start,
             sigfile_block_start * s_.device_block_data_size(),
             sigfile_block_end,
             sigfile_block_end * s_.device_block_data_size(),
             sigfile_block_count,
             sigfile_size,
             num_digests,
             volume_block_count);

  _get_sig_from_end(signatures,original_sig);
  fmt::print("   - original sig: {}\n",original_sig);

  md5_calc(signatures.data(),
           signatures.size() - RSA512_SIG_SIZE,
           digest);
  tdo_rsa_sign(TDO_KEY_APP,
               digest,
               computed_sig);
  
  fmt::print("   - computed sig: {}\n",computed_sig);

  fmt::print("   - match: {}\n",
             (memcmp(original_sig,computed_sig,sizeof(rsa512_sig_t)) == 0));  
}

static
void
_verify_file(TDO::DevStream &s_,
             const u64       start_offset_in_blocks_,
             const u64       size_in_bytes_,
             const char     *key_)
{
  md5_digest_t digest;
  rsa512_sig_t original_sig;
  rsa512_sig_t computed_sig;
  std::vector<char> data;

  fmt::print("   - start block: {}\n"
             "   - file size: {}b\n",
             start_offset_in_blocks_,
             size_in_bytes_);
  s_.read_data_bytes_from_block(data,
                                start_offset_in_blocks_,
                                size_in_bytes_);

  _get_sig_from_end(data,original_sig);
  fmt::print("   - original sig: {}\n",original_sig);

  md5_calc(data.data(),
           data.size() - RSA512_SIG_SIZE,
           digest);
  tdo_rsa_sign(key_,
               digest,
               computed_sig);

  fmt::print("   - computed sig: {}\n",computed_sig);

  fmt::print("   - match: {}\n",
             (memcmp(original_sig,computed_sig,sizeof(rsa512_sig_t)) == 0));  
}


static
void
_verify_romtag_assets(TDO::DevStream &s_)
{
  int size_in_bytes;  
  int offset_in_blocks;
  TDO::ROMTagVec rom_tags;

  rom_tags = s_.romtags();
  for(const auto &rom_tag : rom_tags)
    {
      offset_in_blocks = rom_tag.offset + 1;
      size_in_bytes    = rom_tag.size;

      switch(rom_tag.type)
        {
        case RSA_OS:
        case RSA_MISCCODE:
        case RSA_NEWKNEWNEWGNUBOOT:
          fmt::print(" - Verifying {} with 3DO Key:\n",
                     rom_tag.type_str());          
          ::_verify_file(s_,offset_in_blocks,size_in_bytes,TDO_KEY_3DO);
          break;
        case RSA_APPSPLASH:
          fmt::print(" - Verifying {} with APP Key:\n",
                     rom_tag.type_str());          
          ::_verify_file(s_,offset_in_blocks,size_in_bytes,TDO_KEY_APP);
          break;
        case RSA_SIGNATURE_BLOCK:          
          fmt::print(" - Verifying {} with APP Key:\n",
                     rom_tag.type_str());
          ::_verify_signature_file(s_,rom_tag);
          break;
        }
    }
}

static
void
_verify_rsa_sigs(TDO::DevStream &s_)
{
  _verify_disclabel_romtags_bootcode(s_);
  _verify_romtag_assets(s_);
}

void
Subcommand::verify(const Options::Verify &opts_)
{
  for(const auto &filepath : opts_.filepaths)
    {
      Error err;
      TDO::FileStream stream;

      err = stream.open(filepath);
      if(err)
        {
          fmt::print(stderr,"3dt: {} - {}\n",err.str,filepath);
          continue;
        }

      if(!stream.has_romtags())
        {
          fmt::print(stderr,"3dt: {} does not contain ROMTags\n",filepath);
          continue;
        }

      fmt::print("{}:\n",filepath);
      ::_verify_rsa_sigs(stream);
    }
}
