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

#include "tdo_disc_ids.hpp"

#include <algorithm>


namespace TDO
{
  const
  ID*
  disc_ids_start()
  {
    return tdo_disc_ids_start();
  }

  const
  ID*
  disc_ids_end()
  {
    return tdo_disc_ids_end();
  }

  const
  ID*
  disc_ids_find(const ID &id_,
                const ID *prev_)
  {
    return tdo_disc_ids_find(&id_,prev_);
  }

  void
  disc_ids_find_full_matches(const ID &id_,
                             IDVec    &matches_)
  {
    const TDO::ID *cur;

    cur = NULL;
    while(true)
      {
        cur = TDO::disc_ids_find(id_,cur);
        if(cur == NULL)
          break;
        matches_.push_back(cur);
      }
  }

  void
  disc_ids_find_partial_matches(const ID    &id_,
                                const IDVec &full_matches_,
                                IDVec       &partial_matches_)
  {
    const TDO::ID *cur;

    cur = TDO::disc_ids_start();
    for(; cur->name; cur++)
      {
        if(!tdo_disc_ids_equalish(&id_,cur))
          continue;
        if(std::find(full_matches_.begin(),full_matches_.end(),cur) != full_matches_.end())
          continue;
        partial_matches_.push_back(cur);
      }
  }
}
