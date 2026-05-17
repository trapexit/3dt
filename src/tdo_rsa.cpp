#include "tdo_rsa.h"

#include "bigd.h"
#include "md5.h"

#include <cstring>

struct Bigd
{
  BIGD bd;

  Bigd(BIGD bd_ = nullptr)
    : bd(bd_)
  {
  }

  ~Bigd()
  {
    if(bd)
      bdFree(&bd);
  }

  Bigd(const Bigd&)            = delete;
  Bigd& operator=(const Bigd&) = delete;

  Bigd(Bigd&& other_) noexcept
    : bd(other_.bd)
  {
    other_.bd = nullptr;
  }

  Bigd&
  operator=(Bigd&& other_) noexcept
  {
    if(this != &other_)
      {
        if(bd)
          bdFree(&bd);
        bd = other_.bd;
        other_.bd = nullptr;
      }
    return *this;
  }

  operator BIGD() const
  {
    return bd;
  }
};

extern "C"
{

void
tdo_rsa_sign(const char         *key_,
             const md5_digest_t  digest_,
             rsa512_sig_t        sig_)
{
  md5_digest_t digest;
  std::memcpy(digest,digest_,sizeof(digest));

  Bigd n(tdo_keys_n(key_));
  Bigd d(tdo_keys_d(key_));
  Bigd m(tdo_keys_m(key_,digest));
  Bigd s(bdNew());

  bdModExp(s,m,d,n);

  bdConvToOctets(s,sig_,sizeof(rsa512_sig_t));
}

}
