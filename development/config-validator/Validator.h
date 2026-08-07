/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/OpChainDescriptor.h"
#include "pek/ModelDescriptor.h"

#include <tl/expected.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pek::config {

enum class ValidationPhase { Parse, Dispatch, Schema, Descriptor };

struct ValidationIssue {
    std::string rule;
    ValidationPhase phase;
    std::string file;
    std::string instanceLocation;
    std::optional<std::string> relatedInstanceLocation;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;

    [[nodiscard]] bool ok() const noexcept {
        return issues.empty();
    }

    void sort();
    [[nodiscard]] std::string toText() const;
    [[nodiscard]] std::string toJson() const;
};

namespace detail {
class ValidatedAccess;
}

template <typename T> class Validated {
  public:
    [[nodiscard]] const T &value() const noexcept {
        return value_;
    }

    [[nodiscard]] T intoValue() && {
        return std::move(value_);
    }

  private:
    friend class detail::ValidatedAccess;

    explicit Validated(T value) : value_(std::move(value)) {}

    T value_;
};

template <typename T> using ValidationResult = tl::expected<Validated<T>, ValidationReport>;

[[nodiscard]] ValidationResult<pek::ModelDescriptor>
validateModelJson(std::string_view json, std::string_view source = "model.json");

[[nodiscard]] ValidationResult<pek::op::OpChainDescriptor>
validateOpChainJson(std::string_view json, std::string_view source = "opchain.json");

[[nodiscard]] ValidationReport
validateOpChainSemantics(const pek::op::OpChainDescriptor &descriptor,
                         std::string_view source = "<typed>");

} // namespace pek::config
