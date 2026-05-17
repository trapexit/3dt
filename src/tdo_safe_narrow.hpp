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
#include "types_ints.h"

#include <limits>
#include <string>

namespace TDO
{
  // Narrow an s64 value to u32 with overflow detection. The 3DO
  // OperaFS targets a 32-bit platform; many on-disc fields are u32
  // and intermediate arithmetic uses s64 to keep overflow detectable.
  // Throws Error("<label_> does not fit in 32-bit unsigned: <value>")
  // when value_ is negative or exceeds UINT32_MAX.
  inline u32
  checked_narrow_s64_to_u32(const s64          value_,
                            const std::string &label_)
  {
    if((value_ < 0) ||
       (value_ > static_cast<s64>(std::numeric_limits<u32>::max())))
      throw Error(label_ +
                  " does not fit in 32-bit unsigned: " +
                  std::to_string(value_));
    return static_cast<u32>(value_);
  }

  // Narrow a u64 value to s64 with overflow detection. Used when a
  // computation done in unsigned 64-bit (to avoid signed-overflow UB)
  // must be converted back to s64 for a downstream signed API.
  // Throws Error("<label_> does not fit in 64-bit signed: <value>")
  // when value_ exceeds INT64_MAX.
  inline s64
  checked_narrow_u64_to_s64(const u64          value_,
                            const std::string &label_)
  {
    if(value_ > static_cast<u64>(std::numeric_limits<s64>::max()))
      throw Error(label_ +
                  " does not fit in 64-bit signed: " +
                  std::to_string(value_));
    return static_cast<s64>(value_);
  }

  // Narrow a u64 value to u32 with overflow detection. The 3DO
  // OperaFS targets a 32-bit platform; many on-disc fields are u32
  // and intermediate arithmetic uses u64 (rather than s64) when the
  // value is known non-negative and the concern is just upper bound.
  // Throws Error("<label_> does not fit in 32-bit unsigned: <value>")
  // when value_ exceeds UINT32_MAX.
  inline u32
  checked_narrow_u64_to_u32(const u64          value_,
                            const std::string &label_)
  {
    if(value_ > static_cast<u64>(std::numeric_limits<u32>::max()))
      throw Error(label_ +
                  " does not fit in 32-bit unsigned: " +
                  std::to_string(value_));
    return static_cast<u32>(value_);
  }

  // Narrow a u32 value to u8 with overflow detection. Used when an on-disc
  // OperaFS field is documented as 1 byte (record_type, volume_flags,
  // volume_structure_version) but the source representation is wider
  // (e.g. JSON manifest value comes through as u32). A manifest that
  // specifies a value > 255 is malformed; throwing here surfaces it
  // rather than silently truncating to a different byte than the user
  // wrote.
  inline u8
  checked_narrow_u32_to_u8(const u32          value_,
                           const std::string &label_)
  {
    if(value_ > static_cast<u32>(std::numeric_limits<u8>::max()))
      throw Error(label_ +
                  " does not fit in 8-bit unsigned: " +
                  std::to_string(value_));
    return static_cast<u8>(value_);
  }

  // Checked u32 multiplication: returns a_*b_ in u32, throwing on
  // overflow. Uses __builtin_mul_overflow, portable across every
  // compiler this project targets (gcc, clang, MinGW). The error
  // message reports the mathematically correct product so the user
  // sees the offending value, matching the wording used by the
  // checked_narrow_* helpers above.
  inline u32
  checked_mul_u32(const u32          a_,
                  const u32          b_,
                  const std::string &label_)
  {
    u32 result;
    if(__builtin_mul_overflow(a_,b_,&result))
      throw Error(label_ +
                  " does not fit in 32-bit unsigned: " +
                  std::to_string(static_cast<u64>(a_) * b_));
    return result;
  }

  // Checked u32 addition. Same shape as checked_mul_u32.
  inline u32
  checked_add_u32(const u32          a_,
                  const u32          b_,
                  const std::string &label_)
  {
    u32 result;
    if(__builtin_add_overflow(a_,b_,&result))
      throw Error(label_ +
                  " does not fit in 32-bit unsigned: " +
                  std::to_string(static_cast<u64>(a_) + b_));
    return result;
  }
}
