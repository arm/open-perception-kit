/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace opk {

std::string base64Encode(std::span<const uint8_t> data);

} // namespace opk
