#pragma once

#include "fmt.hpp"

#include "tdo_rsa.h"
#include "types_ints.h"

template<>
struct fmt::formatter<rsa512_sig_t> : formatter<std::string>
{
  template <typename FormatContext>
  auto
  format(const rsa512_sig_t  sig_,
         FormatContext      &ctx_) const
  {
    std::string s;

    for(u64 i = 0; i < sizeof(rsa512_sig_t); i++)
      s += fmt::format("{:02X}",sig_[i]);
    
    return formatter<std::string>::format(s,ctx_);
  }
};
