/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "ValidatorInternal.h"

#include <utility>
#include <variant>

namespace opk::config {
namespace {

template <typename T>
tl::expected<T, ValidationReport>
validateTypedJson(std::string_view json, std::string_view source, detail::DescriptorType type) {
    const auto &schemas = detail::embeddedSchemas();
    if (!schemas.has_value())
        return tl::unexpected{schemas.error()};

    auto result = detail::validateDocument(json, source, *schemas, type);
    if (!result.has_value())
        return tl::unexpected{std::move(result.error())};
    return std::get<T>(std::move(*result));
}

} // namespace

tl::expected<nlohmann::json, ValidationReport> validatePipelineJson(std::string_view json,
                                                                    std::string_view source) {
    return validateTypedJson<nlohmann::json>(json, source, detail::DescriptorType::Pipeline);
}

tl::expected<opk::ModelDescriptor, ValidationReport> validateModelJson(std::string_view json,
                                                                       std::string_view source) {
    return validateTypedJson<opk::ModelDescriptor>(json, source, detail::DescriptorType::Model);
}

tl::expected<opk::op::OpChainDescriptor, ValidationReport>
validateOpChainJson(std::string_view json, std::string_view source) {
    return validateTypedJson<opk::op::OpChainDescriptor>(
        json, source, detail::DescriptorType::OpChain);
}

tl::expected<std::string, ValidationReport> supportedPipelineVersion() {
    const auto &schemas = detail::embeddedSchemas();
    if (!schemas.has_value())
        return tl::unexpected{schemas.error()};
    return schemas->pipelineVersion;
}

tl::expected<std::string, ValidationReport> supportedModelVersion() {
    const auto &schemas = detail::embeddedSchemas();
    if (!schemas.has_value())
        return tl::unexpected{schemas.error()};
    return schemas->modelVersion;
}

tl::expected<std::string, ValidationReport> supportedOpChainVersion() {
    const auto &schemas = detail::embeddedSchemas();
    if (!schemas.has_value())
        return tl::unexpected{schemas.error()};
    return schemas->opchainVersion;
}

ValidationReport validateOpChainSemantics(const opk::op::OpChainDescriptor &descriptor,
                                          std::string_view source) {
    return detail::validateOpChainV1(descriptor, source);
}

} // namespace opk::config
