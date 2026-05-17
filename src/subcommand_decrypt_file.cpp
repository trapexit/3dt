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

#include "subcommand.hpp"

#include "error.hpp"
#include "options.hpp"
#include "tdo_boot_code_crypto.hpp"

#include "fmt.hpp"

#include <fstream>
#include <vector>

static
void
_decrypt_file(std::fstream &fs_)
{
  std::streamsize filesize;
  std::vector<char> data;

  fs_.seekg(0,std::ios::end);
  filesize = fs_.tellg();
  fs_.seekg(0,std::ios::beg);

  data.resize(filesize);
  
  fs_.read(data.data(),filesize);

  TDO::decrypt_boot_code_range(data.data(),
                               TDO::boot_code_crypto_aligned_size(data.size()));

  fs_.seekp(0,std::ios::beg);
  fs_.write((const char*)data.data(),data.size());
}

void
Subcommand::decrypt_file(const Options::DecryptFile &opts_)
{
  bool failed;

  failed = false;
  for(const auto &filepath : opts_.filepaths)
    {
      std::fstream fs;

      fs.open(filepath,std::ios::binary|std::ios::in|std::ios::out);
      if(!fs.is_open())
        {
          fmt::print(stderr,"3dt: error opening {}\n",filepath);
          failed = true;
          continue;
        }

      ::_decrypt_file(fs);

      fs.close();
    }

  if(failed)
    throw Error("decrypt-file failed");
}
