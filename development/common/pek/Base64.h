/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace pek {

std::string base64Encode(std::span<const uint8_t> data);

} // namespace pek
