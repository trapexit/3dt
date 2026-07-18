#include "tdo_rsa.h"

#include "bigd.h"
#include "md5.h"

#include <cstring>

namespace
{
  // Portfolio OS src/dipir/rsadipir.c:PUBLIC_DEMOKEY_MOD. Development
  // systems accept this public-only key after a retail-key check fails.
  static constexpr unsigned char DEMO_KEY_MODULUS[] =
    {
      0x00,0xc0,0x76,0x47,0x97,0xb8,0xbe,0xc8,0x97,0x2a,0x0e,0xd8,
      0xc9,0x0a,0x8c,0x33,0x4d,0xd0,0x49,0xad,0xd0,0x22,0x2c,0x09,
      0xd2,0x0b,0xe0,0xa7,0x9e,0x33,0x89,0x10,0xbc,0xae,0x42,0x20,
      0x60,0x90,0x6a,0xe0,0x22,0x1d,0xe3,0xf3,0xfc,0x74,0x7c,0xcf,
      0x98,0xae,0xcc,0x85,0xd6,0xed,0xc5,0x2d,0x93,0xd5,0xb7,0x39,
      0x67,0x76,0x16,0x05,0x25,
    };
  static constexpr unsigned long DEMO_KEY_EXPONENT = 65537;
}

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

bool
tdo_rsa_verify_demo(const md5_digest_t digest_,
                    const rsa512_sig_t sig_)
{
  md5_digest_t digest;
  Bigd exponent(bdNew());
  Bigd modulus(bdNew());
  Bigd recovered(bdNew());
  Bigd signature(bdNew());

  std::memcpy(digest,digest_,sizeof(digest));
  Bigd expected(tdo_keys_m1_retail_message(digest));
  bdSetShort(exponent,DEMO_KEY_EXPONENT);
  bdConvFromOctets(modulus,DEMO_KEY_MODULUS,sizeof(DEMO_KEY_MODULUS));
  bdConvFromOctets(signature,sig_,sizeof(rsa512_sig_t));
  bdModExp(recovered,signature,exponent,modulus);

  return (bdIsEqual(recovered,expected) != 0);
}

}
