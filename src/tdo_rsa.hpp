#pragma once

#include "md5.h"

#include <cstddef>

inline constexpr std::size_t RSA512_SIG_SIZE = 512 / 8;
inline constexpr char TDO_KEY_3DO[] = "3do";
inline constexpr char TDO_KEY_APP[] = "app";

using rsa512_sig_t = unsigned char[RSA512_SIG_SIZE];

void
tdo_rsa_sign(const char         *key,
             const md5_digest_t  digest,
             rsa512_sig_t        sig);

bool
tdo_rsa_verify_retail(const char         *key,
                      const md5_digest_t  digest,
                      const rsa512_sig_t  sig);

bool
tdo_rsa_verify_development(const md5_digest_t digest,
                           const rsa512_sig_t sig);
