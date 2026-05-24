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

#include "error.hpp"
#include "log.hpp"
#include "options.hpp"
#include "tdo_file_stream.hpp"

#include "fmt.hpp"

#include <fstream>
#include <filesystem>

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

namespace Subcmd
{
  void
  to_iso(const Options::ToISO &options_)
  {
    std::uint64_t blocks;
    std::ofstream ofs;
    fs::path output_path;
    TDO::FileStream stream;
    std::array<char,2048> buf;

    output_path = get_output_path(options_);
    if(output_path == options_.input)
      {
        Log::error({"input == output"});
        throw Error("to-iso failed");
      }

    stream.open(options_.input);

    if(!stream.good())
      {
        Log::error_stream_open(options_.input);
        throw Error("to-iso failed");
      }

    ofs.open(output_path,std::ios::binary|std::ios::trunc);
    if(!ofs.good())
      {
        stream.close();
        Log::error_stream_open(output_path);
        throw Error("to-iso failed");
      }

    const std::uint64_t leading_blocks =
      (stream.data_offset() / stream.device_block_data_size());
    const std::uint64_t device_blocks = stream.device_block_count();

    if(device_blocks > leading_blocks)
      blocks = (device_blocks - leading_blocks);
    else
      blocks = 0;
    try
      {
        for(std::uint64_t block = 0; block < blocks; block++)
          {
            stream.data_byte_seek(block * stream.device_block_data_size());
            stream.read(buf);

            ofs.write(buf.data(),buf.size());
            if(ofs.bad())
              {
                Log::error({"write failed"});
                throw Error("to-iso failed");
              }

            fmt::print("\r{}: block {} of {} written",
                       output_path,
                       block,
                       (blocks == 0 ? 0 : (blocks - 1)));
          }
      }
    catch(const Error &err)
      {
        Log::error(err);
        throw Error("to-iso failed");
      }

    fmt::print("\n");
  }
}
