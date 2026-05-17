#include "tdo_boot_code_crypto.hpp"

#include "error.hpp"

#include <cstring>

namespace
{
  static constexpr u32 DECRYPT_MASK_1 = 0xf0f0f0f0;
  static constexpr u32 DECRYPT_MASK_2 = 0x0f0f0f0f;

  // From portfolio_os/src/dipir/rsadipir.c:DecryptBlock().  The original
  // initializes q from PUBLIC_SIGNATURES_ARRAY, but that value is overwritten
  // before the first dereference and every 16 words after.  In practice
  // PUBLIC_SIGNATURES_ARRAY has no effect; the active key is PUBLIC_THDOKEY_MOD.
  static constexpr u8 BOOT_CODE_CRYPT_KEY[] =
    {
      0x00, 0xB1, 0x94, 0x62, 0xB0, 0x0D, 0x8D, 0x6E,
      0x1E, 0xC9, 0x09, 0xAB, 0x38, 0x5E, 0x06, 0xFE,
      0x03, 0x4B, 0xFD, 0x28, 0x2E, 0x9F, 0xFD, 0xC5,
      0x84, 0x83, 0x8C, 0x15, 0xF1, 0x25, 0x93, 0xDD,
      0x1E, 0x3A, 0x8B, 0x56, 0x26, 0xF1, 0xB9, 0xD0,
      0xED, 0x0C, 0x38, 0x4E, 0xF6, 0xC5, 0xD1, 0x45,
      0x12, 0xBD, 0x72, 0xDD, 0xB8, 0x5B, 0x44, 0x08,
      0x0E, 0x04, 0x72, 0xC0, 0x3D, 0x0A, 0xFC, 0x4C,
    };

  static
  u32
  key_word(const u64 word_offset_)
  {
    u32 key;
    const u64 key_offset = (word_offset_ % 16) * sizeof(u32);

    memcpy(&key,&BOOT_CODE_CRYPT_KEY[key_offset],sizeof(key));

    return key;
  }

  static
  u32
  decrypt_word(const u32 word_,
               const u64 word_offset_)
  {
    const u32 xored = word_ ^ key_word(word_offset_);

    return (((xored & DECRYPT_MASK_1) >> 4) |
            ((xored & DECRYPT_MASK_2) << 4));
  }

  static
  u32
  encrypt_word(const u32 word_,
               const u64 word_offset_)
  {
    const u32 swapped = (((word_ & DECRYPT_MASK_1) >> 4) |
                         ((word_ & DECRYPT_MASK_2) << 4));

    return (swapped ^ key_word(word_offset_));
  }

  static
  void
  crypt_range(void *data_,
              const u64 size_,
              const u64 key_offset_,
              u32 (*crypt_word_)(u32,u64))
  {
    if(((size_ % 4) != 0) || ((key_offset_ % 4) != 0))
      throw Error("boot_code encrypted range is not 4-byte aligned");

    u32 *words = static_cast<u32*>(data_);
    for(u64 i = 0; i < size_ / sizeof(u32); i++)
      words[i] = crypt_word_(words[i], (key_offset_ / sizeof(u32)) + i);

  }
}

u64
TDO::boot_code_crypto_aligned_size(const u64 size_)
{
  return (size_ - (size_ % 4));
}

void
TDO::decrypt_boot_code_range(void *data_,
                             const u64 size_,
                             const u64 key_offset_)
{
  crypt_range(data_,size_,key_offset_,decrypt_word);
}

void
TDO::encrypt_boot_code_range(void *data_,
                             const u64 size_,
                             const u64 key_offset_)
{
  crypt_range(data_,size_,key_offset_,encrypt_word);
}
