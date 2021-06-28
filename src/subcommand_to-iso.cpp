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

#include "error.hpp"
#include "log.hpp"
#include "options.hpp"
#include "tdo_disc_file_reader.hpp"

#include "fmt.hpp"

#include <fstream>

namespace fs = std::filesystem;

static
fs::path
get_output_path(const Options::ToISO &options_)
{
  if(options_.output.empty())
    {
      fs::path rv;
      rv = options_.input;
      rv.replace_extension(".iso");
      return rv;
    }
  else
    {
      return options_.output;
    }
}

namespace Subcommand
{
  void
  to_iso(const Options::ToISO &options_)
  {
    Error err;
    std::int64_t i;
    std::int64_t sectors;
    std::ofstream ofs;
    fs::path output_path;
    TDO::DiscFileReader reader;
    std::array<char,2048> buf;

    output_path = get_output_path(options_);
    if(output_path == options_.input)
      {
        Log::error({"input == output"});
        return;
      }

    reader.open(options_.input);
    if(reader.bad())
      {
        Log::error_stream_open(options_.input);
        return;
      }

    ofs.open(output_path,std::ios::binary);
    if(ofs.bad())
      {
        reader.close();
        Log::error_stream_open(output_path);
        return;
      }

    err = reader.discover_image_format();
    if(err)
      {
        Log::error(err);
        return;
      }

    i = 0;
    sectors = reader.sector_count() - 1;
    while(reader.good() && !reader.eof())
      {
        reader.sector_seek(i);

        reader.read(buf);
        if(!reader.good())
          break;

        ofs.write(buf.data(),buf.size());
        if(ofs.bad())
          {
            Log::error({"write failed"});
            break;
          }

        fmt::print("\r{}: sector {} of {} written",output_path,i,sectors);

        i++;
      }

    fmt::print("\n");
  }
}
