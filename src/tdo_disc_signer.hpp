#pragma once

#include "error.hpp"
#include "tdo_romtag.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace TDO
{
  inline constexpr u32 APP_SPLASH_NTSC_PAYLOAD_SIZE = 153624;
  inline constexpr u32 APP_SPLASH_NTSC_SIGNED_SIZE =
    APP_SPLASH_NTSC_PAYLOAD_SIZE + 64;
  inline constexpr u32 APP_SPLASH_PAL_PAYLOAD_SIZE = 202752;
  inline constexpr u32 APP_SPLASH_PAL_SIGNED_SIZE =
    APP_SPLASH_PAL_PAYLOAD_SIZE + 64;

  struct SignedROMTagPayloadLayout
  {
    u32 payload_size;
    u32 signed_size;
  };

  SignedROMTagPayloadLayout inspect_signed_romtag_payload(u32                      romtag_type,
                                                          const std::vector<char> &data,
                                                          u32                      logical_size_hint = 0,
                                                          u32                      authoritative_size_hint = 0,
                                                          u32                      existing_size_hint = 0);

  void recreate_layout_special_files(const std::filesystem::path &filepath,
                                      bool                         sign_payloads = false,
                                      bool                         mark = false,
                                      bool                         banner_romtag = true,
                                      bool                         billstuff_romtag = false,
                                      const TDO::ROMTagVec        &source_romtags = {});
  void mark_disc_image(const std::filesystem::path &filepath,
                       const std::string           &action);
  void sign_disc_image(const std::filesystem::path &filepath,
                        bool                         mark = false,
                        bool                         preflight = true,
                        bool                         banner_romtag = true,
                        bool                         billstuff_romtag = false,
                        const TDO::ROMTagVec        &source_romtags = {});
}
