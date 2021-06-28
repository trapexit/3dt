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

#include "tdo_disc_ids.h"

#include <vector>

namespace TDO
{
  typedef tdoid_t ID;
  typedef std::vector<const ID*> IDVec;

  const ID* disc_ids_start();
  const ID* disc_ids_end();
  const ID* disc_ids_find(const ID &id,
                          const ID *prev);
  void disc_ids_find_full_matches(const ID &id,
                                  IDVec    &matches);
  void disc_ids_find_partial_matches(const ID    &id,
                                     const IDVec &full_matches,
                                     IDVec       &partial_matches);
}
