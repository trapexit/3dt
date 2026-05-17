#pragma once

#include "types_ints.h"

namespace TDO
{
  u64 boot_code_crypto_aligned_size(u64 size_);

  void decrypt_boot_code_range(void *data_,
                               u64   size_,
                               u64   key_offset_ = 0);
  void encrypt_boot_code_range(void *data_,
                               u64   size_,
                               u64   key_offset_ = 0);
}
