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

#include "subcommand.hpp"

#include "options.hpp"

#include "fmt.hpp"
#include "fmt_rsa512_sig.hpp"
#include "fmt_md5_digest.hpp"

#include <vector>
#include <fstream>


static
void
_sign_file(std::fstream     &fs_,
           const std::string key_name_,
           const u64         length_,
           const bool        write_,
           const bool        append_)
{
  md5_digest_t digest;
  rsa512_sig_t sig;
  std::streamsize filesize;
  std::vector<char> data;

  fs_.seekg(0,std::ios::end);
  filesize = fs_.tellg();
  fs_.seekg(0,std::ios::beg);

  if(!append_)
    filesize -= sizeof(sig);
  
  data.resize(filesize);
  
  fs_.read(data.data(),filesize);

  md5_calc(data.data(),
           data.size(),
           digest);
  tdo_rsa_sign(key_name_.c_str(),
               digest,
               sig);

  fmt::print(" - md5 digest: {}\n"
             " - rsa sig: {}\n",
             digest,
             sig);

  if(write_)
    {
      fs_.seekp(filesize,std::ios::beg);
      fs_.write((const char*)sig,sizeof(sig));
    }
}

void
Subcommand::sign_file(const Options::SignFile &opts_)
{
  for(const auto &filepath : opts_.filepaths)
    {
      std::fstream fs;

      fs.open(filepath,std::ios::binary|std::ios::in|std::ios::out);
      if(!fs.is_open())
        {
          fmt::print(stderr,"3dt: error opening {}\n",filepath);
          break;
        }

      ::_sign_file(fs,opts_.key_name,false,opts_.append);

      fs.close();
    }
}
