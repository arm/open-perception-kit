/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "pek/ModelDescriptor.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>
#include <modelfetch/modelfetch.hpp>
#include <tl/expected.hpp>

#include "pek/File.h"
#include "pek/Result.h"

using namespace pek;
namespace std_fs = std::filesystem;

namespace {

constexpr const char *MaterializedModelsRoot = "/work/var/models";

bool is_safe_relative_path(const std_fs::path &path) {
    if (path.empty() || path.is_absolute())
        return false;
    for (const auto &component : path) {
        if (component == "..")
            return false;
    }
    return path.lexically_normal() != ".";
}

bool is_descendant(const std_fs::path &path, const std_fs::path &root) {
    const std_fs::path relative = path.lexically_relative(root);
    return !relative.empty() && relative != "." && !relative.is_absolute() &&
           *relative.begin() != "..";
}

bool is_lower_hex(std::string_view value) {
    return std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    });
}

bool is_immutable_file_locator(const std::string &assetId) {
    constexpr size_t RevisionLength = 40;
    constexpr std::string_view FileMarker = "#file=";

    const size_t fileMarker = assetId.find(FileMarker);
    if (fileMarker == std::string::npos || fileMarker + FileMarker.size() == assetId.size() ||
        assetId.find('#') != fileMarker)
        return false;

    const size_t revisionMarker = assetId.rfind('@', fileMarker);
    if (revisionMarker == std::string::npos || revisionMarker <= 3 ||
        assetId.find('/', 3) >= revisionMarker || fileMarker != revisionMarker + 1 + RevisionLength)
        return false;

    return is_lower_hex(std::string_view(assetId).substr(revisionMarker + 1, RevisionLength));
}

bool has_canonical_integrity(const std::string &value) {
    constexpr std::string_view Sha256Prefix = "sha256:";
    constexpr std::string_view GitSha1Prefix = "git-sha1:";
    size_t prefixSize = 0;
    if (value.starts_with(Sha256Prefix))
        prefixSize = Sha256Prefix.size();
    else if (value.starts_with(GitSha1Prefix))
        prefixSize = GitSha1Prefix.size();
    else
        return false;

    if (const size_t digestSize = prefixSize == Sha256Prefix.size() ? 64 : 40;
        value.size() != prefixSize + digestSize)
        return false;

    return is_lower_hex(std::string_view(value).substr(prefixSize));
}

pek::Error model_load_cancelled(const std::string &descriptorPath) {
    return PEK_ERROR(
        pek::ErrorFlag::SystemFailure,
        fmt::format("Model materialization cancelled for ModelDescriptor [{}]", descriptorPath));
}

pek::Error modelfetch_error(const std::string &descriptorPath,
                            const std::string &operation,
                            const std::exception &error) {
    return PEK_ERROR(pek::ErrorFlag::InvalidData,
                     fmt::format("modelfetch {} failed for ModelDescriptor [{}]: {}",
                                 operation,
                                 descriptorPath,
                                 error.what()));
}

std::string_view failure_reason_name(modelfetch::asset_download_failure_reason reason) {
    const std::string_view name = magic_enum::enum_name(reason);
    return name.empty() ? "unknown" : name;
}

pek::Result<std_fs::path> materializePublishedModel(const std::string &descriptorPath,
                                                    const std::string &assetId,
                                                    std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return tl::unexpected{model_load_cancelled(descriptorPath)};

    if (!is_immutable_file_locator(assetId)) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("Published ModelDescriptor [{}] must use an immutable canonical "
                                  "file asset locator",
                                  descriptorPath))};
    }

    try {
        const modelfetch::client service;
        const std::array requests{
            modelfetch::asset_download_request(assetId, MaterializedModelsRoot),
        };

        std::vector<modelfetch::asset_download_outcome> outcomes;
        if (stopToken.stop_possible()) {
            auto cancellationCallback = [stopToken](const modelfetch::progress_event &) {
                return stopToken.stop_requested() ? modelfetch::progress_control::abort
                                                  : modelfetch::progress_control::continue_;
            };
            outcomes = service.download_asset_requests(requests, cancellationCallback);
        } else {
            outcomes = service.download_asset_requests(requests);
        }

        if (stopToken.stop_requested())
            return tl::unexpected{model_load_cancelled(descriptorPath)};

        if (outcomes.size() != 1) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("modelfetch returned {} outcomes for ModelDescriptor [{}]",
                                      outcomes.size(),
                                      descriptorPath))};
        }

        const auto &outcome = outcomes.front();
        if (const std::string &returnedAssetId = std::visit(
                [](const auto &value) -> const std::string & { return value.asset_id; }, outcome);
            returnedAssetId != assetId) {
            return tl::unexpected{PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format("modelfetch returned a mismatched asset for ModelDescriptor [{}]",
                            descriptorPath))};
        }

        if (const auto *failure = std::get_if<modelfetch::asset_download_failure>(&outcome)) {
            return tl::unexpected{PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format(
                    "modelfetch could not materialize modelFile in ModelDescriptor [{}]: {}",
                    descriptorPath,
                    failure_reason_name(failure->reason)))};
        }

        const auto &success = std::get<modelfetch::asset_download_success>(outcome);
        if (success.files.size() != 1) {
            return tl::unexpected{
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("modelfetch returned {} model paths for ModelDescriptor [{}]",
                                      success.files.size(),
                                      descriptorPath))};
        }

        const modelfetch::materialized_file &modelFile = success.files.front();
        if (!has_canonical_integrity(modelFile.integrity)) {
            return tl::unexpected{PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format("modelfetch returned invalid integrity for ModelDescriptor [{}]",
                            descriptorPath))};
        }
        return modelFile.path;
    } catch (const modelfetch::callback_aborted &error) {
        if (stopToken.stop_requested())
            return tl::unexpected{model_load_cancelled(descriptorPath)};
        return tl::unexpected{modelfetch_error(descriptorPath, "asset download callback", error)};
    } catch (const std::exception &error) { // NOSONAR: translate all standard SDK failures.
        return tl::unexpected{modelfetch_error(descriptorPath, "asset download", error)};
    }
}

pek::Result<std::string> resolveModelFile(const std::string &descriptorPath,
                                          const ModelDescriptor &descriptor,
                                          std::stop_token stopToken) {
    const bool published = descriptor.modelFile.rfind("hf:", 0) == 0;
    std::error_code ec;
    if (published) {
        auto materializedModel =
            materializePublishedModel(descriptorPath, descriptor.modelFile, stopToken);
        if (!materializedModel)
            return tl::unexpected{materializedModel.error()};
        const std_fs::path workspaceRoot = std_fs::canonical("/work", ec);
        if (ec) {
            return tl::unexpected{PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format("Failed to resolve the PEK workspace for ModelDescriptor [{}]: {}",
                            descriptorPath,
                            ec.message()))};
        }
        const std_fs::path modelStoreRoot = std_fs::canonical(MaterializedModelsRoot, ec);
        if (ec) {
            return tl::unexpected{PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format("Failed to resolve the model store for ModelDescriptor [{}]: {}",
                            descriptorPath,
                            ec.message()))};
        }
        const std_fs::path modelFile = std_fs::canonical(*materializedModel, ec);
        if (ec || !is_descendant(modelStoreRoot, workspaceRoot) ||
            !is_descendant(modelFile, modelStoreRoot) || !std_fs::is_regular_file(modelFile, ec)) {
            return tl::unexpected{PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format("Published ModelDescriptor [{}] modelFile is not a regular file "
                            "inside the model store",
                            descriptorPath))};
        }
        return modelFile.string();
    }

    const std_fs::path relativeModelFile(descriptor.modelFile);
    if (!is_safe_relative_path(relativeModelFile)) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("ModelDescriptor [{}] has an unsafe modelFile [{}]",
                                  descriptorPath,
                                  descriptor.modelFile))};
    }

    const std_fs::path descriptorFile =
        std_fs::weakly_canonical(std_fs::absolute(descriptorPath, ec), ec);
    if (ec) {
        return tl::unexpected{PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format(
                "Failed to resolve ModelDescriptor path [{}]: {}", descriptorPath, ec.message()))};
    }
    const std_fs::path modelRoot = descriptorFile.parent_path();
    const std_fs::path modelFile = std_fs::weakly_canonical(modelRoot / relativeModelFile, ec);
    if (ec || !is_descendant(modelFile, modelRoot)) {
        return tl::unexpected{PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format("ModelDescriptor [{}] modelFile escapes its directory", descriptorPath))};
    }
    return modelFile.string();
}

} // namespace

pek::Result<ModelDescriptor> ModelDescriptor::fromFile(const std::string &path,
                                                       std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return tl::unexpected{model_load_cancelled(path)};

    std::string content = pek::fs::loadTextOrDefault(path, "");

    if (content.empty()) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("ModelDescriptor file [{}] not found or empty", path)));
    }

    auto descriptor = fromJson(content);
    if (!descriptor)
        return descriptor;

    if (stopToken.stop_requested())
        return tl::unexpected{model_load_cancelled(path)};

    auto resolvedModelFile = resolveModelFile(path, *descriptor, stopToken);
    if (!resolvedModelFile)
        return tl::unexpected{resolvedModelFile.error()};
    descriptor->modelFile = *resolvedModelFile;
    return descriptor;
}
