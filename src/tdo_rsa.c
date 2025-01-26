#include "tdo_rsa.h"

#include "bigd.h"
#include "md5.h"

void
tdo_rsa_sign(const char         *key_,
             const md5_digest_t  digest_,
             rsa512_sig_t        sig_)
{
  BIGD n;
  BIGD d;
  BIGD m;
  BIGD s;

  n = tdo_keys_n(key_);
  d = tdo_keys_d(key_);
  m = tdo_keys_m(key_,digest_);
  s = bdNew();

  bdModExp(s,m,d,n);

  bdConvToOctets(s,sig_,sizeof(rsa512_sig_t));

  bdFree(&s);
  bdFree(&m);
  bdFree(&d);
  bdFree(&n);
}
