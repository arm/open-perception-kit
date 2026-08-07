/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "ValidatorInternal.h"

#include <utility>
#include <variant>

namespace pek::config {
namespace detail {

class ValidatedAccess {
  public:
    template <typename T> static Validated<T> make(T value) {
        return Validated<T>(std::move(value));
    }
};

} // namespace detail
namespace {

template <typename T>
ValidationResult<T>
validateTypedJson(std::string_view json, std::string_view source, detail::DescriptorType type) {
    const auto &schemas = detail::embeddedSchemas();
    if (!schemas.has_value())
        return tl::unexpected{schemas.error()};

    auto result = detail::validateDocument(json, source, *schemas, type);
    if (!result.has_value())
        return tl::unexpected{std::move(result.error())};
    return detail::ValidatedAccess::make(std::get<T>(std::move(*result)));
}

} // namespace

ValidationResult<pek::ModelDescriptor> validateModelJson(std::string_view json,
                                                         std::string_view source) {
    return validateTypedJson<pek::ModelDescriptor>(json, source, detail::DescriptorType::Model);
}

ValidationResult<pek::op::OpChainDescriptor> validateOpChainJson(std::string_view json,
                                                                 std::string_view source) {
    return validateTypedJson<pek::op::OpChainDescriptor>(
        json, source, detail::DescriptorType::OpChain);
}

ValidationReport validateOpChainSemantics(const pek::op::OpChainDescriptor &descriptor,
                                          std::string_view source) {
    return detail::validateOpChainV1(descriptor, source);
}

} // namespace pek::config
