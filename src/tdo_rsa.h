#pragma once

#include "md5.h"
#include "tdo_keys.h"

#define RSA512_SIG_SIZE (512 / 8)
#define TDO_KEY_3DO "3do"
#define TDO_KEY_APP "app"

typedef unsigned char rsa512_sig_t[RSA512_SIG_SIZE];

#ifdef __cplusplus
extern "C"
{
#endif
  
void
tdo_rsa_sign(const char         *key,
             const md5_digest_t  digest,
             rsa512_sig_t        sig);

#ifdef __cplusplus
}
#endif

