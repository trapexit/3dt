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
#include "file_digests.hpp"

#include "tdo_file_stream.hpp"
#include "tdo_fs_walker.hpp"

#include "options.hpp"

#include "fmt.hpp"
#include "fmt_rsa512_sig.hpp"
#include "fmt_md5_digest.hpp"
#include "nonstd/string.hpp"

class SignaturesFileUpdater final : public TDO::FSWalker::Callbacks
{
public:
  u32 signatures_file_size;
  
public:
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
    //    else if(lc_filepath == "launchme")
    //      type = RSA_BLOCKS_ALWAYS;
    else if(lc_filepath == "bannerscreen")
      type = RSA_APPSPLASH;

    if(type == 0)
      return;

    TDO::ROMTag romtag;

    romtag.type        = type;
    romtag.sub_systype = 0x0F;
    romtag.size        = record_.byte_count;
    romtag.offset      = record_.avatar_list[0] - 1;
    romtag.version     = 0;
    romtag.revision    = 0;
    switch(type)
      {
      case RSA_SIGNATURE_BLOCK:
        romtag.type_specific = 15;
        break;
      case RSA_OS:
        romtag.version  = 24;
        romtag.revision = 225;
        break;
      case RSA_NEWKNEWNEWGNUBOOT:
        romtag.version  = 2;
        romtag.revision = 5;

        // This should be removable once '3doiso' is replaced. It
        // appears to pad boot_code to 8192 bytes which confuses things.
        {
          md5_digest_t digest;          
          std::vector<char> buf;

          stream_.read_data_bytes_from_block(buf,
                                             record_.avatar_list[0],
                                             5996);
          md5_calc(buf.data(),buf.size(),digest);
          if(!memcmp(digest,MD5_DIGEST_BOOT_CODE,sizeof(md5_digest_t)))
            {
              fmt::print("  - correcting boot_code size to 5996\n");
              romtag.size = 5996;
              stream_.data_byte_seek(record_pos_);
              stream_.data_byte_skip(offsetof(TDO::DirectoryRecord,byte_count));
              stream_.write((u32)romtag.size);
            }
        }
        break;
      }

    fmt::print("{}: {}\n",lc_filepath,romtag.type);
    romtags.emplace_back(romtag); 
  }
};


static
void
_sign_disclabel_romtags_bootcode(TDO::FileStream &s_)
{
  md5_digest_t digest;
  rsa512_sig_t signature;
  std::vector<char> data;
  std::optional<TDO::ROMTag> romtag;

  romtag = s_.romtag(RSA_NEWKNEWNEWGNUBOOT);

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
      fmt::print("{} {}\n",block_pos,s_.good());
      
      s_.read_data_blocks(buf,block_pos,(LOG_BLOCK_SIZE / PHY_BLOCK_SIZE));

      md5_calc(buf.data(),buf.size(),digest);

      signatures.insert(signatures.end(),
                        digest,
                        &digest[sizeof(digest)]);
    }

  //  signatures.resize(3424);
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
  fmt::print("signature file offset: {}\n",sig_offset);
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

      fmt::print("romtag size: {}; signatures size: {}\n",
                 romtag.size,
                 signatures.size());
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
  std::optional<TDO::ROMTag> romtag;
  std::vector<char> data;
  rsa512_sig_t sig;
  md5_digest_t digest;

  romtag = s_.romtag(RSA_APPSPLASH);
  if(!romtag)
    throw std::runtime_error("APPSPLASH romtag is missing!!! OMG!");

  s_.read_data_bytes_from_block(data,
                                romtag->offset + 1,
                                romtag->size - sizeof(rsa512_sig_t));
  md5_calc(data.data(),
           data.size(),
           digest);
  tdo_rsa_sign(TDO_KEY_APP,digest,sig);

  s_.write((const char*)sig,sizeof(sig));
}

static
void
_pad_image_and_update_disclabel(TDO::FileStream &s_)
{
  TDO::DiscLabel dl;

  fmt::print(" - current size: {}b\n",
             s_.size_in_bytes());
  s_.resize_multiple(LOG_BLOCK_SIZE);
  fmt::print(" - padded size:  {}b\n",
             s_.size_in_bytes());
  
  dl = s_.disc_label();

  dl.volume_block_count = s_.size_in_device_blocks();

  s_.data_block_seek(s_.disc_label_block());
  s_.write(dl);
}

static
void
_generate_and_write_romtags(TDO::FileStream &s_)
{
  Error err;
  ROMTagsGenerator tags;
  TDO::FSWalker fswalker(s_,tags);
  
  err = fswalker.walk();
  if(err)
    throw std::runtime_error(err.str);

  s_.data_block_seek(s_.romtags_block());
  for(auto &tag : tags.romtags)
    s_.write(tag);
  s_.write(TDO::ROMTag{});

  err.str = "image is missing file: ";
  if(!s_.romtag(RSA_APPSPLASH))
    throw std::runtime_error(err.str + "BannerScreen");
  if(!s_.romtag(RSA_SIGNATURE_BLOCK))
    throw std::runtime_error(err.str + "signatures");
  if(!s_.romtag(RSA_NEWKNEWNEWGNUBOOT))
    throw std::runtime_error(err.str + "system/kernel/boot_code");
  if(!s_.romtag(RSA_OS))
    throw std::runtime_error(err.str + "system/kernel/os_code");
  if(!s_.romtag(RSA_MISCCODE))
    throw std::runtime_error(err.str + "system/kernel/misc_code");
  if(!s_.romtag(RSA_BLOCKS_ALWAYS))
    throw std::runtime_error(err.str + "launchme");
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

        ::_pad_image_and_update_disclabel(stream);
        ::_generate_and_write_romtags(stream);
        ::_sign_signature_block(stream);
        ::_sign_appsplash(stream);        
        ::_sign_disclabel_romtags_bootcode(stream);

        stream.close();
      }
  }
}
