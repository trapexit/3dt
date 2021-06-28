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

#include "tdo_disc_packer.hpp"

#include "tdo_disc_label.hpp"
#include "tdo_directory_record.hpp"

#include <filesystem>
#include <utility>
#include <memory>


namespace TDO
{
  class Entry
  {
  public:
    typedef std::unique_ptr<Entry> UPtr;

  public:
    bool is_file() const { return !is_directory(); }
    bool is_directory() const { return record.is_directory(); }

  public:
    TDO::DirectoryRecord record;
  };

  class File : public Entry
  {
  public:
  };

  class Directory : public Entry
  {
  public:
    void add_file(const TDO::DirectoryRecord &record);
    void add_directory(const TDO::DirectoryRecord &record);

  public:
    std::vector<Entry::UPtr> entries;
  };

  class DiscPacker
  {
  public:
    typedef std::filesystem::path Path;

  public:
    DiscPacker();

  public:
    void add_record(const Path &path, const TDO::DirectoryRecord &record);

  public:
    void compute_layout();

  public:
    int write(std::ostream &os) const;

  private:
    Directory root;
  };
}

void
TDO::DiscPacker::add_record(const Path                 &path_,
                            const TDO::DirectoryRecord &record_)
{
  Entry::UPtr file;

  file = std::make_unique<File>();

  root.entries.push_back(std::move(file));
}

void
TDO::DiscPacker::compute_layout()
{

}

int
TDO::DiscPacker::write(std::ostream &os_) const
{

  return 0;
}
