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
#include "tdo_rsa.h"

#include "error.hpp"
#include "subcommand.hpp"

#include "options.hpp"

#include "fmt.hpp"
#include "fmt_rsa512_sig.hpp"
#include "fmt_md5_digest.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  static
  std::vector<char>
  read_file_prefix(const fs::path &filepath_,
                   const u64       size_)
  {
    std::ifstream is;
    std::vector<char> data;

    data.resize(size_);
    is.open(filepath_,std::ios::binary);
    if(!is)
      throw Error("error opening " + filepath_.string());
    if(size_ > 0)
      is.read(data.data(),data.size());
    if(!is)
      throw Error("error reading " + filepath_.string());

    return data;
  }

  static
  void
  read_signature_trailer(const fs::path &filepath_,
                         rsa512_sig_t   sig_)
  {
    std::ifstream is;

    is.open(filepath_,std::ios::binary);
    if(!is)
      throw Error("error opening " + filepath_.string());
    is.seekg(-static_cast<std::streamoff>(sizeof(rsa512_sig_t)),std::ios::end);
    is.read((char*)sig_,sizeof(rsa512_sig_t));
    if(!is)
      throw Error("error reading signature trailer from " + filepath_.string());
  }

  static
  void
  write_signature(const fs::path   &filepath_,
                  const u64         offset_,
                  const rsa512_sig_t sig_)
  {
    std::fstream fs;

    fs.open(filepath_,std::ios::binary|std::ios::in|std::ios::out);
    if(!fs)
      throw Error("error opening " + filepath_.string());
    fs.seekp(offset_,std::ios::beg);
    fs.write((const char*)sig_,sizeof(rsa512_sig_t));
    if(!fs)
      throw Error("error writing signature to " + filepath_.string());
  }

  static
  void
  write_signature_output(const fs::path   &filepath_,
                         const rsa512_sig_t sig_)
  {
    std::ofstream os;

    os.open(filepath_,std::ios::binary|std::ios::trunc);
    if(!os)
      throw Error("error opening signature output " + filepath_.string());
    os.write((const char*)sig_,sizeof(rsa512_sig_t));
    if(!os)
      throw Error("error writing signature output " + filepath_.string());
  }

  static
  u64
  signed_data_size(const u64  filesize_,
                   const bool replace_,
                   const bool verify_)
  {
    if(!replace_ && !verify_)
      return filesize_;
    if(filesize_ < sizeof(rsa512_sig_t))
      throw Error("file is too small to contain a signature trailer");

    return (filesize_ - sizeof(rsa512_sig_t));
  }

  static
  void
  sign_file_one(const fs::path          &filepath_,
                const Options::SignFile &opts_)
  {
    md5_digest_t digest;
    rsa512_sig_t original_sig;
    rsa512_sig_t sig;
    std::vector<char> data;

    const u64 filesize = fs::file_size(filepath_);
    const u64 data_size = signed_data_size(filesize,opts_.replace,opts_.verify);
    data = read_file_prefix(filepath_,data_size);

    md5_calc(data.data(),data.size(),digest);
    tdo_rsa_sign(opts_.key_name.c_str(),digest,sig);

    fmt::print("{}:\n"
               " - key: {}\n"
               " - md5 digest: {}\n"
               " - rsa sig: {}\n",
               filepath_,
               opts_.key_name,
               digest,
               sig);

    if(opts_.verify)
      {
        read_signature_trailer(filepath_,original_sig);
        const bool matched = (memcmp(original_sig,sig,sizeof(rsa512_sig_t)) == 0);

        fmt::print(" - original sig: {}\n"
                   " - match: {}\n",
                   original_sig,
                   matched);
        if(!matched)
          throw Error("signature mismatch");
      }

    if(!opts_.signature_output.empty())
      write_signature_output(opts_.signature_output,sig);

    if(opts_.write)
      write_signature(filepath_,(opts_.append ? filesize : data_size),sig);
  }
}

void
Subcommand::sign_file(const Options::SignFile &opts_)
{
  bool failed;

  failed = false;
  if(opts_.write && (opts_.append == opts_.replace))
    throw Error("--write requires exactly one of --append or --replace");
  if(!opts_.signature_output.empty() && (opts_.filepaths.size() != 1))
    throw Error("--signature-output requires exactly one input file");

  for(const auto &filepath : opts_.filepaths)
    {
      try
        {
          sign_file_one(filepath,opts_);
        }
      catch(const std::exception &e)
        {
          fmt::print(stderr,"3dt: {} - {}\n",e.what(),filepath);
          failed = true;
        }
    }

  if(failed)
    throw Error("sign-file failed");
}
