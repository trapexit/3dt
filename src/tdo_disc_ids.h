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

#ifndef TDO_DISC_IDS_INCLUDED
#define TDO_DISC_IDS_INCLUDED

#include "extern_c.h"

#include <stdint.h>

EXTERN_C_BEGIN

typedef struct tdoid_t tdoid_t;
struct tdoid_t
{
  const char *name;
  const char *volume_id;
  uint32_t    volume_unique_id;
  uint32_t    volume_block_count;
  uint32_t    root_unique_id;
  uint32_t    file_count;
  uint32_t    total_data_size;
};

const tdoid_t* tdo_disc_ids_start();
const tdoid_t* tdo_disc_ids_end();
const tdoid_t* tdo_disc_ids_find_by_vui(const uint32_t  vui,
                                        const tdoid_t  *prev);
const tdoid_t* tdo_disc_ids_find_by_rui(const uint32_t  rui,
                                        const tdoid_t  *prev);
const tdoid_t* tdo_disc_ids_find(const tdoid_t *id,
                                 const tdoid_t *prev);
int tdo_disc_ids_equal(const tdoid_t *a,
                       const tdoid_t *b);
int tdo_disc_ids_equalish(const tdoid_t *a,
                          const tdoid_t *b);

EXTERN_C_END

#endif
