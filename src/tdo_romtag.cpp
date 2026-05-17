#include "tdo_romtag.hpp"

#include "error.hpp"
#include "fmt.hpp"
#include "tdo_dev_stream.hpp"

#include <limits>


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

u64
TDO::safe_romtag_first_data_block(TDO::DevStream    &stream_,
                                  const TDO::ROMTag &tag_,
                                  const char        *label_)
{
  const u64 file_blocks = stream_.device_block_count();
  if(tag_.offset == std::numeric_limits<uint32_t>::max())
    throw Error(std::string(label_) + " ROM tag offset would wrap on +1");
  // offset + 1 must point at file data, not the disc-label / romtags
  // reserved region. The signer rejects avatar == 0 upstream
  // (tdo_disc_signer.cpp), so an offset of zero here only arises from
  // a malformed image. Reject it explicitly with a clearer message
  // than letting downstream code read romtag bytes as tag data.
  if(tag_.offset == 0)
    throw Error(std::string(label_) +
                " ROM tag offset is zero (first data block 1 overlaps reserved region)");
  if((file_blocks > 0) && (static_cast<u64>(tag_.offset) + 1 >= file_blocks))
    throw Error(std::string(label_) + " ROM tag offset is past end of image");

  return (static_cast<u64>(tag_.offset) + 1);
}

u64
TDO::safe_romtag_payload_range(TDO::DevStream    &stream_,
                               const TDO::ROMTag &tag_,
                               const u64          payload_size_bytes_,
                               const char        *label_)
{
  const u64 first_block = safe_romtag_first_data_block(stream_,tag_,label_);
  if(payload_size_bytes_ == 0)
    return first_block;

  const u64 data_block_size = stream_.device_block_data_size();
  if(data_block_size == 0)
    throw Error(std::string(label_) + " stream has zero data-block size");

  const s64 image_size_signed = stream_.size_in_bytes();
  if(image_size_signed < 0)
    throw Error(std::string(label_) + " image size unavailable");
  const u64 image_size = static_cast<u64>(image_size_signed);

  // first_block * data_block_size in u64. file_blocks already bounded
  // first_block above, but the multiplication itself is the gate when
  // device_block_count() returned 0 (sentinel) — guard it explicitly.
  if(first_block > (std::numeric_limits<u64>::max() / data_block_size))
    throw Error(std::string(label_) +
                " ROM tag payload start overflows file offset math");
  const u64 start_offset = first_block * data_block_size;
  if(start_offset > image_size)
    throw Error(std::string(label_) +
                " ROM tag payload starts past end of image");
  if(payload_size_bytes_ > (image_size - start_offset))
    throw Error(std::string(label_) +
                " ROM tag payload extends past end of image");

  return first_block;
}
