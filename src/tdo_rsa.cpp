#include "tdo_rsa.hpp"

#include "bigd.h"
#include "md5.h"
#include "tdo_keys.hpp"

#include <cstddef>
#include <cstring>

namespace
{
  // Portfolio OS src/dipir/rsadipir.c:PUBLIC_DEMOKEY_MOD. Development
  // Dipir builds test this key before the configured primary key.
  static constexpr unsigned char DEMO_KEY_MODULUS[] =
    {
      0x00,0xc0,0x76,0x47,0x97,0xb8,0xbe,0xc8,0x97,0x2a,0x0e,0xd8,
      0xc9,0x0a,0x8c,0x33,0x4d,0xd0,0x49,0xad,0xd0,0x22,0x2c,0x09,
      0xd2,0x0b,0xe0,0xa7,0x9e,0x33,0x89,0x10,0xbc,0xae,0x42,0x20,
      0x60,0x90,0x6a,0xe0,0x22,0x1d,0xe3,0xf3,0xfc,0x74,0x7c,0xcf,
      0x98,0xae,0xcc,0x85,0xd6,0xed,0xc5,0x2d,0x93,0xd5,0xb7,0x39,
      0x67,0x76,0x16,0x05,0x25,
    };
  // Portfolio OS src/dipir/rsadipir.c:PUBLIC_ENGKEY_MOD.  `WHO_KNOWS`
  // is explicitly undefined there, so this is the engineering modulus used
  // by unencrypted, demo, and null Dipir builds.
  static constexpr unsigned char ENGINEERING_KEY_MODULUS[] =
    {
      0x00,0xcc,0xdd,0xb7,0xd6,0x09,0x84,0xa4,0xa7,0xff,0x68,0x41,
      0xea,0xf1,0xb4,0xdf,0x8e,0xdd,0x26,0xbd,0x31,0x5c,0xa7,0x26,
      0x81,0x05,0x8d,0xe1,0x2f,0x47,0x7a,0x5a,0x5b,0x84,0xd4,0xf2,
      0xe3,0x2d,0xd4,0x8b,0xdb,0x8b,0x4a,0x04,0x17,0x6c,0x8f,0x96,
      0xd5,0xb1,0x94,0xc2,0x70,0xa3,0x05,0x93,0xa9,0xea,0x40,0x32,
      0xd0,0x03,0x8c,0xae,0x2d,
    };
  static constexpr unsigned long DEVELOPMENT_KEY_EXPONENT = 65537;
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

namespace
{
  static
  bool
  verify_with_modulus(const md5_digest_t digest_,
                      const rsa512_sig_t sig_,
                      const BIGD         modulus_)
  {
    md5_digest_t digest;
    Bigd exponent(bdNew());
    Bigd recovered(bdNew());
    Bigd signature(bdNew());

    std::memcpy(digest,digest_,sizeof(digest));
    Bigd expected(tdo_keys_m1_retail_message(digest));
    bdSetShort(exponent,DEVELOPMENT_KEY_EXPONENT);
    bdConvFromOctets(signature,sig_,sizeof(rsa512_sig_t));
    bdModExp(recovered,signature,exponent,modulus_);

    return (bdIsEqual(recovered,expected) != 0);
  }

  static
  bool
  verify_with_public_key(const md5_digest_t  digest_,
                         const rsa512_sig_t  sig_,
                         const unsigned char *modulus_,
                         const std::size_t    modulus_size_)
  {
    Bigd modulus(bdNew());

    bdConvFromOctets(modulus,modulus_,modulus_size_);

    return verify_with_modulus(digest_,sig_,modulus);
  }
}

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
tdo_rsa_verify_retail(const char         *key_,
                      const md5_digest_t  digest_,
                      const rsa512_sig_t  sig_)
{
  Bigd modulus(tdo_keys_n(key_));

  return verify_with_modulus(digest_,sig_,modulus);
}

bool
tdo_rsa_verify_development(const md5_digest_t digest_,
                           const rsa512_sig_t sig_)
{
  return (verify_with_public_key(digest_,
                                 sig_,
                                 DEMO_KEY_MODULUS,
                                 sizeof(DEMO_KEY_MODULUS)) ||
          verify_with_public_key(digest_,
                                 sig_,
                                 ENGINEERING_KEY_MODULUS,
                                 sizeof(ENGINEERING_KEY_MODULUS)));
}
