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

#include <cstdint>
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
    Path        layout;
    std::string format;
  };

  struct Pack
  {
    Path        input;
    Path        output;
    Path        layout;
    std::string volume_commentary;
    std::string volume_label;
    bool        banner_romtag = true;
    bool        billstuff_romtag = false;
    bool        dry_run = false;
    bool        mark = true;
    bool        sign = false;
    bool        root_unique_identifier_set = false;
    bool        volume_unique_identifier_set = false;
    uint32_t    root_unique_identifier = 0;
    uint32_t    signature_digest_check_count = 0;
    uint32_t    volume_unique_identifier = 0;
  };

  struct Repack
  {
    PathVec     filepaths;
    Path        output;
    bool        banner_romtag = true;
    bool        billstuff_romtag = false;
    bool        mark = true;
    bool        sign = false;
    uint32_t    signature_digest_check_count = 0;
  };

  struct Rename
  {
    PathVec filepaths;
    bool    take_first;
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

  struct Verify
  {
    PathVec     filepaths;
    std::string format = "human";
    bool        digest_table = true;
    bool        quiet = false;
  };

  struct Sign
  {
    PathVec  filepaths;
    Path     output;
    bool     banner_romtag = true;
    bool     billstuff_romtag = false;
    bool     force = false;
    bool     mark = true;
    uint32_t signature_digest_check_count = 0;
  };

  struct SignFile
  {
    PathVec     filepaths;
    Path        signature_output;
    std::string key_name;
    bool        append = false;
    bool        replace = false;
    bool        verify = false;
    bool        write = false;
  };

  struct DecryptFile
  {
    PathVec     filepaths;
  };

  struct EncryptFile
  {
    PathVec     filepaths;
  };

  List     list     = {};
  Info     info     = {};
  Identify identify = {};
  Unpack   unpack   = {};
  Pack     pack     = {};
  Repack   repack   = {};
  Rename   rename   = {};
  ToISO    to_iso   = {};
  ROMTags  romtags  = {};
  Verify   verify   = {};
  Sign     sign     = {};
  SignFile signfile = {};
  DecryptFile decryptfile = {};
  EncryptFile encryptfile = {};
};
