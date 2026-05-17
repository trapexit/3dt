#pragma once

#include "error.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace TDO
{
  Error recreate_layout_special_files(const std::filesystem::path &filepath,
                                      bool                         sign_payloads = false,
                                      bool                         mark = false,
                                      bool                         banner_romtag = true,
                                      std::uint8_t                 digest_check_count = 15);
  Error mark_disc_image(const std::filesystem::path &filepath,
                        const std::string           &action);
  Error sign_disc_image(const std::filesystem::path &filepath,
                        bool                         mark = false,
                        bool                         preflight = true,
                        bool                         banner_romtag = true,
                        std::uint8_t                 digest_check_count = 15);
}
