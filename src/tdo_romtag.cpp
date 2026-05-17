#include "tdo_romtag.hpp"

#include "fmt.hpp"


std::string
TDO::ROMTag::type_str(const uint8_t type_)
{
  switch(type_)
    {
    case RSA_MUST_RSA:
      return "MUST_RSA";
    case RSA_NEWKNEWNEWGNUBOOT:
      return "NEWKNEWNEWGNUBOOT";
    case RSA_OS:
      return "OS";
    case RSA_BILLSTUFF:
      return "BILLSTUFF";
    case RSA_BLOCKS_ALWAYS:
      return "BLOCKS_ALWAYS";
    case RSA_MISCCODE:
      return "MISCCODE";
    case RSA_SIGNATURE_BLOCK:
      return "SIGNATURE_BLOCK";
    case RSA_APPSPLASH:
      return "APPSPLASH";
    case RSA_DEPOTCONFIG:
      return "DEPOTCONFIG";
    case RSA_DEVICE_INFO:
      return "DEVICE_INFO";
    case RSA_DEV_PERMS:
      return "DEV_PERMS";
    case RSA_BOOT_OVERLAY:
      return "BOOT_OVERLAY";

    case RSA_M2_OS:
      return "M2_OS";
    case RSA_M2_MISCCODE:
      return "M2_MISCCODE";
    case RSA_M2_DRIVER:
      return "M2_DRIVER";
    case RSA_M2_DEVDIPIR:
      return "M2_DEVDIPIR";
    case RSA_M2_APPBANNER:
      return "M2_APPBANNER";
    case RSA_M2_APP_KEYS:
      return "M2_APP_KEYS";
    case RSA_OPERA_CD_IMAGE:
      return "OPERA_CD_IMAGE";
    case RSA_M2_ICON:
      return "M2_ICON";
    }

  return fmt::format("{:#04x}",type_);
}

std::string
TDO::ROMTag::type_str() const
{
  return type_str(type);
}
