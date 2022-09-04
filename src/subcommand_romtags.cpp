/*
  ISC License

  Copyright (c) 2021, Antonio SJ Musumeci <trapexit@spawn.link>

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

#include "options.hpp"
#include "tdo_romtag.hpp"
#include "tdo_file_stream.hpp"

#include "fmt.hpp"
#include "CSVWriter.h"

#include <array>

static
void
print_romtags_csv_header(void)
{
  CSVWriter csv(",");

  csv.newRow();
  csv << "File"
      << "SubSysType"
      << "Type"
      << "Version"
      << "Revision"
      << "Flags"
      << "TypeSpecific"
      << "Offset"
      << "Size";
  fmt::print("{}\n",csv.toString());
}

static
void
romtags_csv(const std::filesystem::path &filepath_,
            TDO::DevStream              &stream_)
{
  Error err;
  TDO::ROMTag tag;
  CSVWriter csv(",");

  stream_.data_block_seek(1);

  stream_.read(tag);
  while(tag.sub_systype || tag.type)
    {
      csv.newRow();
      csv << filepath_.string()
          << fmt::format("{:#04x}",tag.sub_systype)
          << fmt::format("{:#04x}",tag.type)
          << fmt::format("{}",tag.version)
          << fmt::format("{}",tag.revision)
          << fmt::format("{:x}",tag.flags)
          << fmt::format("{}",tag.type_specific)
          << fmt::format("{}",tag.offset)
          << fmt::format("{}",tag.size);

      stream_.read(tag);
    }

  fmt::print("{}\n",csv.toString());
}

static
std::string
rsanode_type(const uint8_t type_)
{
  switch(type_)
    {
    case RSA_MUST_RSA:
      return "MUST_RSA";
    case RSA_NEWKNEWNEWGNUBOOT:
      return "NEWKNEWNEWGNUBOOT";
    case RSA_OS:
      return "OS";
    case RSA_BILLSTUFF:
      return "BILLSTUFF";
    case RSA_BLOCKS_ALWAYS:
      return "BLOCKS_ALWAYS";
    case RSA_MISCCODE:
      return "MISCCODE";
    case RSA_SIGNATURE_BLOCK:
      return "SIGNATURE_BLOCK";
    case RSA_APPSPLASH:
      return "APPSPLASH";
    }

  return fmt::format("{:04x}",type_);
}

static
void
romtags_human(const std::filesystem::path &filepath_,
              TDO::DevStream              &stream_)
{
  Error err;
  TDO::ROMTag tag;
  CSVWriter csv(",");

  stream_.data_block_seek(1);

  stream_.read(tag);
  while(tag.sub_systype || tag.type)
    {
      csv.newRow();
      csv << filepath_.string()
          << fmt::format("{:#04x}",tag.sub_systype)
          << fmt::format("{}",rsanode_type(tag.type))
          << fmt::format("{}",tag.version)
          << fmt::format("{}",tag.revision)
          << fmt::format("{:x}",tag.flags)
          << fmt::format("{}",tag.type_specific)
          << fmt::format("{}",tag.offset)
          << fmt::format("{}",tag.size);

      stream_.read(tag);
    }

  fmt::print("{}\n",csv.toString());
}

void
Subcommand::romtags(const Options::ROMTags &opts_)
{
  print_romtags_csv_header();
  for(const auto &filepath : opts_.filepaths)
    {
      Error err;
      TDO::FileStream stream;

      err = stream.open(filepath);
      if(err)
        {
          fmt::print(stderr,"3dt: {} - {}\n",err.str,filepath);
          break;
        }

      if(stream.is_romfs())
        {
          fmt::print(stderr,"3dt: {} does not contain ROMTags\n",filepath);
          break;
        }

      if(opts_.format == "csv")
        ::romtags_csv(filepath,stream);
      else
        ::romtags_human(filepath,stream);
    }
}
