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

#include "tdo_disc_identifier.hpp"

#include "tdo_dev_stream.hpp"

#include <string>

static
std::string
get_ext_based_on_type(const TDO::DevStream &stream_)
{
  if((stream_.device_block_size() == 2048) && (stream_.device_block_header() == 0))
    return "iso";
  if((stream_.device_block_size() == 2352) && (stream_.device_block_header() == 16))
    return "bin";
  if((stream_.device_block_size() == 4) && (stream_.device_block_header() == 0))
    return "bin";

  return "unknown";
}

static
void
find_matches(const TDO::DiscLabel       &label_,
             const TDO::FilesystemStats &fsstats_,
             TDO::IDVec                 &full_matches_,
             TDO::IDVec                 &partial_matches_)
{
  TDO::ID id;

  id.name               = NULL;
  id.volume_id          = &label_.volume_identifier[0];
  id.volume_unique_id   = label_.volume_unique_identifier;
  id.volume_block_count = label_.volume_block_count;
  id.root_unique_id     = label_.root_unique_identifier;
  id.file_count         = fsstats_.file_count;
  id.total_data_size    = fsstats_.total_data_size;

  TDO::disc_ids_find_full_matches(id,full_matches_);
  TDO::disc_ids_find_partial_matches(id,full_matches_,partial_matches_);
}

TDO::DiscIdentifier::DiscIdentifier()
{
}

Error
TDO::DiscIdentifier::identify(std::istream &is_)
{
  Error err;
  TDO::DevStream stream(is_);

  err = stream.setup();
  if(err)
    return err;

  stream.data_byte_seek(0);
  stream.read(label);

  err = fsstats.collect(stream);
  if(err)
    return err;

  ::find_matches(label,fsstats,full_matches,partial_matches);
  disc_image_ext = ::get_ext_based_on_type(stream);

  return {};
}
