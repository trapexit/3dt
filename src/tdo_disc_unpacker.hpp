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

#pragma once

#include "error.hpp"
#include "tdo_disc_walker.hpp"

#include <filesystem>
#include <istream>
#include <memory>

namespace TDO
{
  class DiscUnpacker
  {
  public:
    typedef std::unique_ptr<DiscUnpacker> Ptr;

  public:
    struct Callback
    {
      typedef std::unique_ptr<Callback> Ptr;

      virtual void before(const std::filesystem::path &path,
                          const TDO::DirectoryRecord  &record) = 0;
      virtual void after(const std::filesystem::path &path,
                         const TDO::DirectoryRecord  &record,
                         const int                    err) = 0;
    };

  public:
    DiscUnpacker(std::istream &is,
                 Callback     &cb);
    ~DiscUnpacker();

  public:
    Error unpack(const std::filesystem::path &dstpath);

  private:
    class Impl;
    std::unique_ptr<Impl> _impl;
  };
}
