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

#include "tdo_rsa.h"

#include "tdo_file_stream.hpp"
#include "tdo_fs_walker.hpp"

#include "options.hpp"

#include "fmt.hpp"
#include "fmt_rsa512_sig.hpp"
#include "nonstd/string.hpp"

class SignaturesFileUpdater final : public TDO::FSWalker::Callbacks
{
public:
  u32 signatures_file_size;
  
public:
  void
  begin()
  {
  }

  void
  end()
  {
  }

  void
  operator()(const std::filesystem::path &filepath_,
             const TDO::DirectoryHeader  &dh_,
             TDO::DevStream              &stream_)
  {

  }

  void
  operator()(const std::filesystem::path &filepath_,
             const TDO::DirectoryRecord  &record_,
             const u32                    record_pos_,
             TDO::DevStream              &s_)
  {
    std::string lc_filepath;
    
    lc_filepath = nonstd::string::as_lowercase(filepath_.string());
    if(lc_filepath != "signatures")
      return;

    s_.data_byte_seek(record_pos_);
    s_.data_byte_skip(offsetof(TDO::DirectoryRecord,byte_count));
    s_.write(signatures_file_size);
    s_.write((u32)((signatures_file_size + (2048 - 1)) / 2048));
  }
};  


class ROMTagsGenerator final : public TDO::FSWalker::Callbacks
{
public:
  TDO::ROMTagVec romtags;

public:
  void
  begin()
  {
  }

  void
  end()
  {
  }

  void
  operator()(const std::filesystem::path &filepath_,
             const TDO::DirectoryHeader  &dh_,
             TDO::DevStream              &stream_)
  {

  }
  
  void
  operator()(const std::filesystem::path &filepath_,
             const TDO::DirectoryRecord  &record_,
             const u32                    record_pos_,
             TDO::DevStream              &stream_)
  {
    u32 type;
    std::string lc_filepath;
    
    type = 0;
    lc_filepath = nonstd::string::as_lowercase(filepath_.string());
    if(lc_filepath == "signatures")
      type = RSA_SIGNATURE_BLOCK;
    else if(lc_filepath == "system/kernel/boot_code")
      type = RSA_NEWKNEWNEWGNUBOOT; // version, revision isn't used?
                                    // picked up by 3DO AIF header, cdipir.c
    else if(lc_filepath == "system/kernel/misc_code")
      type = RSA_MISCCODE;
    else if(lc_filepath == "system/kernel/os_code")
      type = RSA_OS;
    else if(lc_filepath == "launchme")
      type = RSA_BLOCKS_ALWAYS;
    else if(lc_filepath == "bannerscreen")
      type = RSA_APPSPLASH;

    if(type == 0)
      return;

    romtags.emplace_back();
    romtags.back().type        = type;
    romtags.back().sub_systype = 0x0F;
    romtags.back().size        = record_.byte_count;
    romtags.back().offset      = record_.avatar_list[0] - 1;
    romtags.back().version     = 0;
    romtags.back().revision    = 0;
    switch(type)
      {
      case RSA_SIGNATURE_BLOCK:
        romtags.back().type_specific = 15;
        break;
      case RSA_OS:
        romtags.back().version  = 24;
        romtags.back().revision = 255;
        break;
      case RSA_NEWKNEWNEWGNUBOOT:
        romtags.back().version  = 2;
        romtags.back().revision = 5;
        romtags.back().size = 5996;
        break;
      }
  }
};

static
void
_generate_and_write_romtags(TDO::FileStream &s_)
{
  ROMTagsGenerator tags;
  TDO::FSWalker fsw(s_,tags);
  
  auto err = fsw.walk();
  if(err)
    throw std::runtime_error("fuck, shit broken");
  
  s_.data_block_seek(s_.romtags_block());
  for(auto &tag : tags.romtags)
    s_.write(tag);
  s_.write(TDO::ROMTag{});
}


static
void
_sign_disclabel_romtags_bootcode(TDO::FileStream &s_)
{
  md5_digest_t digest;
  rsa512_sig_t signature;
  std::vector<char> data;
  std::optional<TDO::ROMTag> romtag;

  romtag = s_.romtag(RSA_NEWKNEWNEWGNUBOOT);
  if(!romtag)
    {
      fmt::print("- No NEWKNEWNEWGNUBOOT romtag found.\n");
      return;
    }

  s_.read_data_bytes_from_block(data,
                                 s_.disc_label_block(),
                                 s_.disc_label_size_in_bytes());
  s_.read_data_bytes_from_block(data,
                                 s_.romtags_block(),
                                 s_.romtags_size_in_bytes());
  s_.read_data_bytes_from_block(data,
                                romtag->offset + 1,
                                romtag->size);

  md5_calc(data.data(),
           data.size(),
           digest);
  tdo_rsa_sign(TDO_KEY_APP,digest,signature);

  fmt::print(" - Signing DiscLabel + ROMTags + BootCode with APP key\n"
             "   - signature: {}\n",
             signature);

  s_.data_block_seek(s_.romtags_block());
  s_.data_byte_skip(s_.romtags_size_in_bytes());
  s_.write((char*)signature,sizeof(signature));
}

#define PHY_BLOCK_SIZE (2 * 1024)
#define LOG_BLOCK_SIZE (32 * 1024)

// Maybe just assume ISO w/ contiguous 2048 byte block size?
static
void
_sign_signature_block(TDO::FileStream &s_)
{
  md5_digest_t digest;
  rsa512_sig_t sig;
  u64 num_digests;
  u64 volume_block_count;
  std::vector<char> buf;
  std::vector<char> signatures;

  volume_block_count = s_.disc_label().volume_block_count;
  num_digests        = ((volume_block_count * PHY_BLOCK_SIZE) / LOG_BLOCK_SIZE);
  
  fmt::print("block count: {}; num digests: {}\n",
             volume_block_count,num_digests);
  for(u64 i = 0; i < num_digests; i++)
    {
      s64 block_pos;

      block_pos = ((i * LOG_BLOCK_SIZE) / PHY_BLOCK_SIZE);

      buf.clear();
      s_.read_data_blocks(buf,block_pos,(LOG_BLOCK_SIZE / PHY_BLOCK_SIZE));

      md5_calc(buf.data(),buf.size(),digest);

      signatures.insert(signatures.end(),
                        digest,
                        &digest[sizeof(digest)]);
    }

  fmt::print("{} {} {} {}\n",
             signatures.size(),
             signatures.size() / sizeof(digest),
             num_digests,
             s_.romtag(RSA_SIGNATURE_BLOCK)->size - signatures.size());

  md5_calc(signatures.data(),signatures.size(),digest);
  tdo_rsa_sign(TDO_KEY_APP,digest,sig);
  fmt::print("sig: {}\n",sig);

  signatures.resize(signatures.size() + sizeof(sig));
  signatures.resize(((signatures.size() + (PHY_BLOCK_SIZE-1)) / PHY_BLOCK_SIZE) * PHY_BLOCK_SIZE);
  signatures.resize(signatures.size() - sizeof(sig));
  signatures.insert(signatures.end(),
                    sig,
                    sig + sizeof(sig));

  s64 sig_offset = s_.romtag(RSA_SIGNATURE_BLOCK)->offset + 1;
  for(u64 i = 0; i < (signatures.size() / PHY_BLOCK_SIZE); i++)
    {
      fmt::print("offset: {}\n",sig_offset+i);
      s_.data_block_seek(sig_offset + i);
      s_.write(&signatures[i * PHY_BLOCK_SIZE],PHY_BLOCK_SIZE);
    }

  // Correct SIGNATURE_BLOCK romtag
  s_.data_block_seek(s_.romtags_block());
  while(true)
    {
      u64 offset;
      TDO::ROMTag romtag;

      offset = s_.file_tell();
      s_.read(romtag);
      if((romtag.sub_systype == 0) || (romtag.type == 0))
        break;
      if(romtag.type != RSA_SIGNATURE_BLOCK)
        continue;

      if(romtag.size < signatures.size())
        throw std::runtime_error("signatures file too small, increase size and rebuild image");
      
      s_.file_seek(offset);
      s_.data_byte_skip(offsetof(TDO::ROMTag,size));
      s_.write((u32)signatures.size());
      break;
    }

  // Correct OperaFS
  SignaturesFileUpdater sfu;
  TDO::FSWalker fsw(s_,sfu);

  sfu.signatures_file_size = signatures.size();
  auto err = fsw.walk();
  if(err)
    throw std::runtime_error("broken sig write");
}

static
void
_sign_appsplash(TDO::FileStream &s_)
{
  
}

namespace Subcommand
{
  void
  sign(const Options::Sign &opts_)
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

        fmt::print("{}:\n",filepath);
        stream.resize_multiple(LOG_BLOCK_SIZE);
        ::_generate_and_write_romtags(stream);
        ::_sign_disclabel_romtags_bootcode(stream);
        ::_sign_signature_block(stream);
      }
  }
}
