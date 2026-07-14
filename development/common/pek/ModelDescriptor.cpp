/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "pek/ModelDescriptor.h"
#include "fmt/color.h"
#include "tl/expected.hpp"

#include <array>
#include <filesystem>
#include <string>

#include <glib.h>

#include "pek/File.h"
#include "pek/Result.h"

#include "pek/AttributeMap.h"

using namespace pek;
namespace std_fs = std::filesystem;

namespace {

constexpr const char *MaterializedModelsRoot = "/work/var/models";
constexpr const char *ModelfetchExecutable = "/opt/pek-venvs/model-tools/bin/modelfetch";

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

pek::Result<std_fs::path> resolvePublishedModelEntry(const std::string &descriptorPath,
                                                     const std::string &assetId) {
    if (assetId.find("#file=") == std::string::npos) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("Published ModelDescriptor [{}] must identify one file asset",
                                  descriptorPath))};
    }

    std::array<std::string, 4> argumentStorage = {
        ModelfetchExecutable,
        "models",
        "resolve-path",
        assetId,
    };
    std::array<gchar *, 5> arguments = {
        argumentStorage[0].data(),
        argumentStorage[1].data(),
        argumentStorage[2].data(),
        argumentStorage[3].data(),
        nullptr,
    };
    gchar *standardOutput = nullptr;
    gint waitStatus = 0;
    GError *spawnError = nullptr;
    const gboolean spawned = g_spawn_sync(nullptr,
                                          arguments.data(),
                                          nullptr,
                                          G_SPAWN_DEFAULT,
                                          nullptr,
                                          nullptr,
                                          &standardOutput,
                                          nullptr,
                                          &waitStatus,
                                          &spawnError);
    const std::string output = standardOutput == nullptr ? "" : standardOutput;
    g_free(standardOutput);

    if (!spawned) {
        const std::string detail = spawnError == nullptr ? "unknown error" : spawnError->message;
        g_clear_error(&spawnError);
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("Failed to start modelfetch for ModelDescriptor [{}]: {}",
                                  descriptorPath,
                                  detail))};
    }

    GError *waitError = nullptr;
    if (!g_spawn_check_wait_status(waitStatus, &waitError)) {
        const std::string detail = waitError == nullptr ? "unknown error" : waitError->message;
        g_clear_error(&waitError);
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("modelfetch rejected modelFile in ModelDescriptor [{}]: {}",
                                  descriptorPath,
                                  detail))};
    }

    if (output.size() < 2 || output.back() != '\n' || output.find('\n') != output.size() - 1 ||
        output.find('\r') != std::string::npos ||
        !g_utf8_validate(output.data(), static_cast<gssize>(output.size() - 1), nullptr)) {
        return tl::unexpected{PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format("modelfetch returned an invalid model path for ModelDescriptor [{}]",
                        descriptorPath))};
    }

    std_fs::path relativePath(output.substr(0, output.size() - 1));
    if (!is_safe_relative_path(relativePath) || relativePath != relativePath.lexically_normal()) {
        return tl::unexpected{PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format("modelfetch returned an unsafe model path for ModelDescriptor [{}]",
                        descriptorPath))};
    }
    return relativePath;
}

pek::Result<std::string> resolveModelFile(const std::string &descriptorPath,
                                          const ModelDescriptor &descriptor) {
    const bool published = descriptor.modelFile.rfind("hf:", 0) == 0;
    std::error_code ec;
    if (published) {
        auto relativeModelFile = resolvePublishedModelEntry(descriptorPath, descriptor.modelFile);
        if (!relativeModelFile)
            return tl::unexpected{relativeModelFile.error()};
        const std_fs::path workspaceRoot = std_fs::weakly_canonical("/work", ec);
        const std_fs::path modelStoreRoot = std_fs::weakly_canonical(MaterializedModelsRoot, ec);
        const std_fs::path modelFile =
            std_fs::weakly_canonical(modelStoreRoot / *relativeModelFile, ec);
        if (ec || !is_descendant(modelStoreRoot, workspaceRoot) ||
            !is_descendant(modelFile, modelStoreRoot)) {
            return tl::unexpected{PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format("Published ModelDescriptor [{}] modelFile escapes the model store",
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

pek::Result<ModelDescriptor> ModelDescriptor::fromJson(const std::string &jsonString) {
    try {
        json json = json::parse(jsonString);
        return json.get<ModelDescriptor>();
    } catch (const json::exception &e) {
        return tl::unexpected(PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format("Error occured while parsing ModelDescriptor json: {}", e.what())));
    }
}

pek::Result<ModelDescriptor> ModelDescriptor::fromFile(const std::string &path) {
    std::string content = pek::fs::loadTextOrDefault(path, "");

    if (content.empty()) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("ModelDescriptor file [{}] not found or empty", path)));
    }

    auto descriptor = fromJson(content);
    if (!descriptor)
        return descriptor;

    auto resolvedModelFile = resolveModelFile(path, *descriptor);
    if (!resolvedModelFile)
        return tl::unexpected{resolvedModelFile.error()};
    descriptor->modelFile = *resolvedModelFile;
    return descriptor;
}
