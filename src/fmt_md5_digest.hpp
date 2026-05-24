#pragma once

#include "fmt.hpp"

#include "md5.h"
#include "types_ints.h"

template<>
struct fmt::formatter<md5_digest_t> : formatter<std::string>
{
  template <typename FormatContext>
  auto
  format(const md5_digest_t  digest_,
         FormatContext      &ctx_) const
  {
    std::string s;

    for(u64 i = 0; i < sizeof(md5_digest_t); i++)
      s += fmt::format("{:02X}",digest_[i]);
    
    return formatter<std::string>::format(s,ctx_);
  }
};
