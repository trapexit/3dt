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

#include <filesystem>
#include <string>
#include <vector>

struct Options
{
public:
  typedef std::filesystem::path Path;
  typedef std::vector<Path>     PathVec;

public:
  struct List
  {
    Path filepath;
    Path prefix_filter;
    std::string format;
  };

  struct Info
  {
    PathVec     filepaths;
    std::string format;
  };

  struct Identify
  {
    PathVec     filepaths;
    std::string format;
  };

  struct Unpack
  {
    PathVec     filepaths;
    Path        output;
    std::string format;
  };

  struct Pack
  {
  };

  struct Rename
  {
    PathVec filepaths;
    bool    take_first;
  };

  struct Crc32b
  {
    PathVec filepaths;
  };

  struct ToISO
  {
    Path input;
    Path output;
  };

  struct ROMTags
  {
    PathVec     filepaths;
    std::string format;
  };

  List     list     = {};
  Info     info     = {};
  Identify identify = {};
  Unpack   unpack   = {};
  Pack     pack     = {};
  Rename   rename   = {};
  Crc32b   crc32b   = {};
  ToISO    to_iso   = {};
  ROMTags  romtags  = {};
};
