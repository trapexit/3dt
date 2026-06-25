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

#include "md5.h"
#include "tdo_romtag.hpp"
#include "types_ints.h"

#include <vector>

namespace TDO
{
  struct ROMTagVersionRevisionFallback
  {
    md5_digest_t md5;
    u8           type;
    u8           version;
    u8           revision;
  };

  bool
  romtag_has_version_revision(const TDO::ROMTag &romtag_);

  const ROMTagVersionRevisionFallback*
  find_romtag_version_revision_fallback(const u8                 type_,
                                        const std::vector<char> &data_);
}
