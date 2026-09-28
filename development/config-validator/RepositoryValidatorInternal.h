/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include "Validator.h"

#include <filesystem>

namespace opk::config::detail {

[[nodiscard]] ValidationReport validateRepository(const std::filesystem::path &root);

} // namespace opk::config::detail
