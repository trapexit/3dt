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
#include "tdo_disc_reader.hpp"

#include <filesystem>
#include <functional>
#include <istream>
#include <memory>

namespace TDO
{
  class DiscWalker
  {
  public:
    struct Callbacks
    {
      virtual
      void
      operator()(const std::filesystem::path&,
                 const TDO::DirectoryHeader&,
                 TDO::DiscReader&)
      {
      }

      virtual
      void
      operator()(const std::filesystem::path&,
                 const TDO::DirectoryRecord&,
                 TDO::DiscReader&)
      {
      }
    };

  public:
    DiscWalker(std::istream &is,
               Callbacks    &callbacks);
    DiscWalker(TDO::DiscReader &reader,
               Callbacks       &callbacks);

  public:
    Error walk();

  private:
    Callbacks    &_callbacks;
    std::istream &_is;
  };
}
