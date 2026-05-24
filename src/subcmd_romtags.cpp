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

#include "subcmd.hpp"

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
      << "TypeName"
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
  TDO::ROMTagVec tags;
  CSVWriter csv(",");

  tags = stream_.romtags();
  for(const auto &tag : tags)
    {
      csv.newRow();
      csv << filepath_.string()
          << fmt::format("{:#04x}",tag.sub_systype)
          << fmt::format("{:#04x}",tag.type)
          << fmt::format("{}",tag.type_str())
          << fmt::format("{}",tag.version)
          << fmt::format("{}",tag.revision)
          << fmt::format("{:x}",tag.flags)
          << fmt::format("{}",tag.type_specific)
          << fmt::format("{}",tag.offset)
          << fmt::format("{}",tag.size);
    }

  fmt::print("{}\n",csv.toString());
}

static
void
romtags_human(const std::filesystem::path &filepath_,
               TDO::DevStream              &stream_)
{
  TDO::ROMTagVec tags;

  tags = stream_.romtags();
  fmt::print("{}:\n", filepath_.filename());
  for(const auto &tag : tags)
    {
       fmt::print("  - Offset:      {}\n"
                  "    SubSysType:  {:#04x}\n"
                  "    Type:        {:#04x} ({})\n"
                  "    Version:     {}\n"
                  "    Revision:    {}\n"
                  "    Flags:       {:#x}\n"
                  "    TypeSpec:    {}\n"
                  "    Size:        {}\n",
                  tag.offset,
                  tag.sub_systype,
                  tag.type,
                  tag.type_str(),
                  tag.version,
                  tag.revision,
                  tag.flags,
                  tag.type_specific,
                 tag.size);
    }
}

void
Subcmd::romtags(const Options::ROMTags &opts_)
{
  bool failed;
  bool printed_header = false;

  failed = false;
  for(const auto &filepath : opts_.filepaths)
    {
      try
        {
          TDO::FileStream stream;

          stream.open(filepath);

          if(!stream.has_romtags())
            {
              fmt::print(stderr,"3dt: {} does not contain ROMTags\n",filepath);
              failed = true;
              continue;
            }

          if(opts_.format == "csv")
            {
              if(printed_header == false)
                {
                  print_romtags_csv_header();
                  printed_header = true;
                }
              romtags_csv(filepath,stream);
            }
          else
            {
              romtags_human(filepath,stream);
            }
        }
      catch(const std::exception &e)
        {
          fmt::print(stderr,"3dt: {} - {}\n",e.what(),filepath);
          failed = true;
        }
    }

  if(failed)
    throw Error("romtags failed");
}
