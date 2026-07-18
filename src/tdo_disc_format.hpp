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

#pragma once

#include "types_ints.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace TDO
{
  constexpr u64 BLOCK_SIZE = 2048;

  static inline
  u64
  round_up(const u64 number_,
           const u64 multiple_)
  {
    return (((number_ + multiple_ - 1) / multiple_) * multiple_);
  }

  static inline
  u64
  div_round_up(const u64 number_,
               const u64 multiple_)
  {
    return ((number_ + multiple_ - 1) / multiple_);
  }

  static inline
  std::string
  display_path(const std::filesystem::path &parent_,
               const std::string           &filename_)
  {
    std::string path = parent_.generic_string();
    if(!path.empty() && !filename_.empty() && (filename_[0] != '/'))
      path += "/";
    if(filename_.empty())
      path += "<invalid empty filename>";
    else
      path += filename_;
    return path;
  }
}
