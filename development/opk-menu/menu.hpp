/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace opk::menu {

struct Entry {
    std::string_view id;
    std::string_view description;
    std::string_view source_info;
};

enum class ResultKind {
    Selected,
    Cancelled,
    Interrupted,
};

struct Result {
    ResultKind kind{ResultKind::Cancelled};
    size_t selected_index{};
    int signal_number{};
};

Result select_pipeline(std::string_view pipelines_directory,
                       std::span<const Entry> entries,
                       std::optional<size_t> last_pipeline_index);

} // namespace opk::menu
