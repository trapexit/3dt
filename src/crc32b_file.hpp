#pragma once

#include "types_ints.h"

#include <filesystem>

u32 crc32b_file(const std::filesystem::path&);
