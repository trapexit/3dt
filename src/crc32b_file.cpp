#include "crc32b_file.hpp"

#include "crc32b.h"

#include <fstream>
#include <vector>


u32
crc32b_file(const std::filesystem::path &filepath_)
{
  std::ifstream fs;
  std::vector<char> buf;
  u32 rv;

  fs.open(filepath_, std::ios::binary);
  if(!fs.good())
    throw std::runtime_error("failed to open file for crc32b calculation");

  rv = crc32b_start();

  buf.resize(64 * 1024);
  while(true)
    {
      fs.read(buf.data(),buf.size());
      if(fs.gcount() == 0)
        break;
      if(fs.bad())
        throw std::runtime_error("error reading file for crc32b calculation");

      rv = crc32b_continue(buf.data(),
                           static_cast<uint32_t>(fs.gcount()),
                           rv);
    }

  rv = crc32b_finish(rv);

  fs.close();

  return rv;
}
