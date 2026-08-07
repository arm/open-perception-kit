/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Validator.h"

#include <filesystem>

namespace pek::config::detail {

[[nodiscard]] ValidationReport validateRepository(const std::filesystem::path &root);

} // namespace pek::config::detail
