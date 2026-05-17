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

#include "tdo_filesystem_stats.hpp"

#include "tdo_fs_walker.hpp"
#include "tdo_safe_narrow.hpp"


class FSStatsCollector final : public TDO::FSWalker::Callbacks
{
public:
  FSStatsCollector()
    : file_count(0),
      total_data_size(0)
  {
  }

public:
  void
  begin()
  {
  }

  void
  end()
  {
  }

public:
  void
  operator()(const std::filesystem::path&,
             const TDO::DirectoryHeader&,
             TDO::DevStream&)
  {
  }

  void
  operator()(const std::filesystem::path &path_,
             const TDO::DirectoryRecord  &record_,
             const uint32_t               record_pos_,
             TDO::DevStream              &stream_)
  {
    file_count++;
    total_data_size += record_.byte_count;
  }

  Error
  invalid_filename(const std::filesystem::path&,
                   const std::string&,
                   const TDO::DirectoryRecord &record_,
                   const uint32_t,
                   const Error&,
                   TDO::DevStream&)
  {
    file_count++;
    total_data_size += record_.byte_count;

    return {};
  }

public:
  void
  collect(TDO::DevStream &stream_)
  {
    TDO::FSWalker walker(stream_,*this);

    walker.walk();
  }

public:
  // Accumulate in u64 so that a malformed image whose records claim
  // more bytes than 32-bit can express is detected at collect() time
  // rather than silently wrapping into a plausible-looking value.
  // The OperaFS image-size invariant is u32-bounded; overflowing u32
  // here means the image is malformed.
  uint64_t file_count;
  uint64_t total_data_size;
};

namespace TDO
{
  FilesystemStats::FilesystemStats()
    : file_count(0),
      total_data_size(0)
  {
  }

  void
  FilesystemStats::collect(TDO::DevStream &stream_)
  {
    FSStatsCollector collector;

    collector.collect(stream_);

    file_count      = TDO::checked_narrow_u64_to_u32(collector.file_count,
                                                     "filesystem file_count");
    total_data_size = TDO::checked_narrow_u64_to_u32(collector.total_data_size,
                                                     "filesystem total_data_size");
  }
}
