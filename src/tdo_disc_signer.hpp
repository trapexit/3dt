#pragma once

#include "error.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace TDO
{
  void recreate_layout_special_files(const std::filesystem::path &filepath,
                                     bool                         sign_payloads = false,
                                     bool                         mark = false,
                                     bool                         banner_romtag = true,
                                     std::uint8_t                 digest_check_count = 0);
  void mark_disc_image(const std::filesystem::path &filepath,
                       const std::string           &action);
  void sign_disc_image(const std::filesystem::path &filepath,
                       bool                         mark = false,
                       bool                         preflight = true,
                       bool                         banner_romtag = true,
                       std::uint8_t                 digest_check_count = 0);
}
