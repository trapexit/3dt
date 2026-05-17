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
#include "tdo_safe_narrow.hpp"

#include "fmt.hpp"

#include <fstream>
#include <vector>

static
void
_encrypt_file(std::fstream &fs_)
{
  // Mirrors Portfolio OS CD-DIPIR inverse transform for boot payloads in
  // `portfolio_os/src/dipir/cdipir.c` (DecryptBlock()).
  std::vector<char> data;

  fs_.seekg(0,std::ios::end);
  if(fs_.fail())
    throw Error("encrypt-file: failed to seek to end of file");

  const std::streampos end_pos = fs_.tellg();
  if(end_pos == std::streampos(-1))
    throw Error("encrypt-file: failed to read file size");

  fs_.seekg(0,std::ios::beg);
  if(fs_.fail())
    throw Error("encrypt-file: failed to seek to start of file");

  // OperaFS targets a 32-bit platform; any input file must fit in u32
  // bytes. Route through checked_narrow_s64_to_u32 so an oversized
  // file (or a -1 sentinel from a failed tellg that slipped past the
  // explicit check above) produces a clear error rather than a
  // bad_alloc from data.resize(SIZE_MAX) or a silent wrap downstream.
  const s64 filesize_signed = static_cast<s64>(end_pos);
  if(filesize_signed < 0)
    throw Error("encrypt-file: negative file size");
  const u32 filesize =
    TDO::checked_narrow_s64_to_u32(filesize_signed,"encrypt-file file size");

  data.resize(filesize);

  fs_.read(data.data(),static_cast<std::streamsize>(filesize));
  if(fs_.fail() && !fs_.eof())
    throw Error("encrypt-file: failed to read file contents");

  TDO::encrypt_boot_code_range(data.data(),
                               TDO::boot_code_crypto_aligned_size(data.size()));

  fs_.seekp(0,std::ios::beg);
  if(fs_.fail())
    throw Error("encrypt-file: failed to seek for write");
  fs_.write(data.data(),static_cast<std::streamsize>(filesize));
  if(fs_.fail())
    throw Error("encrypt-file: failed to write file contents");
}

void
Subcommand::encrypt_file(const Options::EncryptFile &opts_)
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

      _encrypt_file(fs);

      fs.close();
    }

  if(failed)
    throw Error("encrypt-file failed");
}
