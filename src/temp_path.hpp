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

#include "error.hpp"
#include "fmt.hpp"

#include <filesystem>
#include <random>
#include <string>

static inline
std::filesystem::path
temp_path_for(const std::filesystem::path &path_)
{
  // The .tmpN suffix scheme would be predictable to a same-user
  // attacker who can pre-populate the slots. Mix in two random_device
  // samples per attempt so the candidate names are not enumerable
  // from the input filename alone. Matches the style used in
  // create_temp_dir() in subcmd_repack.cpp (commit 9d8659c).
  std::random_device rd;
  for(std::uint32_t i = 0; i < 1000; i++)
    {
      std::filesystem::path path = path_;
      path += fmt::format(".tmp{:08x}{:08x}",rd(),rd());
      if(!std::filesystem::exists(path))
        return path;
    }

  throw Error("failed to create temporary output path");
}
