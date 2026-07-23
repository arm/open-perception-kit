/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "pek/ModelDescriptor.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <fmt/color.h>
#include <glib.h>
#include <modelfetch.h>
#include <tl/expected.hpp>

#include "pek/AttributeMap.h"
#include "pek/File.h"
#include "pek/Result.h"

using namespace pek;
namespace std_fs = std::filesystem;

namespace {

constexpr const char *MaterializedModelsRoot = "/work/var/models";

using ModelfetchService = std::unique_ptr<modelfetch_service_t, decltype(&modelfetch_service_free)>;
using ModelfetchRequest = std::unique_ptr<modelfetch_request_t, decltype(&modelfetch_request_free)>;
using ModelfetchRequestList =
    std::unique_ptr<modelfetch_request_list_t, decltype(&modelfetch_request_list_free)>;
using ModelfetchOutcomeList =
    std::unique_ptr<modelfetch_outcome_list_t, decltype(&modelfetch_outcome_list_free)>;
using ModelfetchError = std::unique_ptr<modelfetch_error_t, decltype(&modelfetch_error_free)>;

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
    for (const char character : value) {
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
            return false;
    }
    return true;
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
    const size_t prefixSize = value.starts_with(Sha256Prefix)    ? Sha256Prefix.size()
                              : value.starts_with(GitSha1Prefix) ? GitSha1Prefix.size()
                                                                 : 0;
    const size_t digestSize = prefixSize == Sha256Prefix.size() ? 64 : 40;
    if (prefixSize == 0 || value.size() != prefixSize + digestSize)
        return false;
    return is_lower_hex(std::string_view(value).substr(prefixSize));
}

std::string copy_text_view(const modelfetch_text_view_t &view) {
    if (view.ptr == nullptr || view.len == 0)
        return {};
    return {reinterpret_cast<const char *>(view.ptr), view.len};
}

pek::Error model_load_cancelled(const std::string &descriptorPath) {
    return PEK_ERROR(
        pek::ErrorFlag::SystemFailure,
        fmt::format("Model materialization cancelled for ModelDescriptor [{}]", descriptorPath));
}

pek::ModelLoadProgressState map_progress_state(modelfetch_progress_state_t state) {
    switch (state) {
    case MODELFETCH_PROGRESS_STARTED:
        return pek::ModelLoadProgressState::Started;
    case MODELFETCH_PROGRESS_IN_PROGRESS:
        return pek::ModelLoadProgressState::InProgress;
    case MODELFETCH_PROGRESS_COMPLETED:
        return pek::ModelLoadProgressState::Completed;
    case MODELFETCH_PROGRESS_FAILED:
        return pek::ModelLoadProgressState::Failed;
    default:
        return pek::ModelLoadProgressState::Unknown;
    }
}

using ProgressBytesGetter = modelfetch_status_t (*)(const modelfetch_progress_event_t *,
                                                    uint8_t *,
                                                    const modelfetch_big_uint_t **);

std::optional<std::string> read_progress_bytes(const modelfetch_progress_event_t *event,
                                               ProgressBytesGetter getter) {
    uint8_t present = 0;
    const modelfetch_big_uint_t *value = nullptr;
    if (getter(event, &present, &value) != MODELFETCH_STATUS_OK || present == 0 || value == nullptr)
        return std::nullopt;

    modelfetch_text_view_t view{nullptr, 0};
    if (modelfetch_big_uint_decimal(value, &view) != MODELFETCH_STATUS_OK || view.ptr == nullptr ||
        view.len == 0)
        return std::nullopt;
    return copy_text_view(view);
}

struct ModelfetchProgressBridge {
    const pek::ModelLoadContext *loadContext = nullptr;
    std::atomic<bool> callbackFailed = false;
};

modelfetch_callback_result_t report_modelfetch_progress(const modelfetch_progress_event_t *event,
                                                        void *userData) noexcept {
    auto *bridge = static_cast<ModelfetchProgressBridge *>(userData);
    if (bridge == nullptr || bridge->loadContext == nullptr || event == nullptr)
        return MODELFETCH_CALLBACK_ABORT;

    const pek::ModelLoadContext &loadContext = *bridge->loadContext;
    if (loadContext.stopRequested())
        return MODELFETCH_CALLBACK_ABORT;

    if (loadContext.progress) {
        try {
            pek::ModelLoadProgress progress;

            modelfetch_text_view_t artifactView{nullptr, 0};
            if (modelfetch_progress_event_artifact_id(event, &artifactView) ==
                MODELFETCH_STATUS_OK) {
                progress.artifactId = copy_text_view(artifactView);
            }

            modelfetch_progress_state_t state = MODELFETCH_PROGRESS_FAILED;
            if (modelfetch_progress_event_state(event, &state) == MODELFETCH_STATUS_OK)
                progress.state = map_progress_state(state);

            progress.transferredBytes =
                read_progress_bytes(event, modelfetch_progress_event_transferred_bytes);
            progress.totalBytes = read_progress_bytes(event, modelfetch_progress_event_total_bytes);

            uint8_t percentagePresent = 0;
            double percentage = 0.0;
            if (modelfetch_progress_event_percentage(event, &percentagePresent, &percentage) ==
                    MODELFETCH_STATUS_OK &&
                percentagePresent != 0) {
                progress.percentage = percentage;
            }

            loadContext.progress(progress);
        } catch (...) {
            bridge->callbackFailed.store(true, std::memory_order_relaxed);
            return MODELFETCH_CALLBACK_ABORT;
        }
    }

    return loadContext.stopRequested() ? MODELFETCH_CALLBACK_ABORT : MODELFETCH_CALLBACK_CONTINUE;
}

std::string modelfetch_error_detail(modelfetch_error_t *rawError) {
    ModelfetchError error(rawError, modelfetch_error_free);
    if (!error)
        return "no error detail";

    modelfetch_text_view_t view{nullptr, 0};
    if (modelfetch_error_message(error.get(), &view) != MODELFETCH_STATUS_OK)
        return "unreadable error detail";
    const std::string detail = copy_text_view(view);
    return detail.empty() ? "empty error detail" : detail;
}

pek::Error modelfetch_error(const std::string &descriptorPath,
                            const std::string &operation,
                            modelfetch_status_t status,
                            modelfetch_error_t *rawError = nullptr) {
    return PEK_ERROR(pek::ErrorFlag::InvalidData,
                     fmt::format("modelfetch {} failed for ModelDescriptor [{}] (status {}): {}",
                                 operation,
                                 descriptorPath,
                                 status,
                                 modelfetch_error_detail(rawError)));
}

pek::Result<std::string> read_text(const std::string &descriptorPath,
                                   const std::string &field,
                                   const modelfetch_text_view_t &view) {
    if (view.ptr == nullptr || view.len == 0 || view.len > G_MAXSSIZE ||
        !g_utf8_validate(
            reinterpret_cast<const gchar *>(view.ptr), static_cast<gssize>(view.len), nullptr)) {
        return tl::unexpected{PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format(
                "modelfetch returned invalid {} for ModelDescriptor [{}]", field, descriptorPath))};
    }
    return copy_text_view(view);
}

const char *failure_reason_name(modelfetch_failure_reason_t reason) {
    switch (reason) {
    case MODELFETCH_FAILURE_SNAPSHOT_UNAVAILABLE:
        return "snapshot_unavailable";
    case MODELFETCH_FAILURE_SNAPSHOT_IDENTITY_MISMATCH:
        return "snapshot_identity_mismatch";
    case MODELFETCH_FAILURE_ANCHOR_NOT_FOUND:
        return "anchor_not_found";
    case MODELFETCH_FAILURE_MANIFEST_INVALID:
        return "manifest_invalid";
    case MODELFETCH_FAILURE_PATH_UNSAFE:
        return "path_unsafe";
    case MODELFETCH_FAILURE_OUTPUT_COLLISION:
        return "output_collision";
    case MODELFETCH_FAILURE_DESTINATION_INVALID:
        return "destination_invalid";
    case MODELFETCH_FAILURE_INTEGRITY_METADATA_UNAVAILABLE:
        return "integrity_metadata_unavailable";
    case MODELFETCH_FAILURE_INTEGRITY_MISMATCH:
        return "integrity_mismatch";
    case MODELFETCH_FAILURE_DOWNLOAD_FAILED:
        return "download_failed";
    case MODELFETCH_FAILURE_MATERIALIZATION_FAILED:
        return "materialization_failed";
    default:
        return "unknown";
    }
}

pek::Result<std_fs::path> materializePublishedModel(const std::string &descriptorPath,
                                                    const std::string &assetId,
                                                    const pek::ModelLoadContext &loadContext) {
    if (loadContext.stopRequested())
        return tl::unexpected{model_load_cancelled(descriptorPath)};

    if (!is_immutable_file_locator(assetId)) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("Published ModelDescriptor [{}] must use an immutable canonical "
                                  "file asset locator",
                                  descriptorPath))};
    }

    modelfetch_service_t *rawService = nullptr;
    modelfetch_error_t *rawError = nullptr;
    modelfetch_status_t status = modelfetch_service_new(&rawService, &rawError);
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{
            modelfetch_error(descriptorPath, "service creation", status, rawError)};
    ModelfetchService service(rawService, modelfetch_service_free);

    modelfetch_request_t *rawRequest = nullptr;
    rawError = nullptr;
    status = modelfetch_request_new(reinterpret_cast<const uint8_t *>(assetId.data()),
                                    assetId.size(),
                                    reinterpret_cast<const uint8_t *>(MaterializedModelsRoot),
                                    std::char_traits<char>::length(MaterializedModelsRoot),
                                    &rawRequest,
                                    &rawError);
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{
            modelfetch_error(descriptorPath, "request creation", status, rawError)};
    ModelfetchRequest request(rawRequest, modelfetch_request_free);

    modelfetch_request_list_t *rawRequests = nullptr;
    rawError = nullptr;
    status = modelfetch_request_list_new(&rawRequests, &rawError);
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{
            modelfetch_error(descriptorPath, "request-list creation", status, rawError)};
    ModelfetchRequestList requests(rawRequests, modelfetch_request_list_free);

    rawError = nullptr;
    status = modelfetch_request_list_append(requests.get(), request.get(), &rawError);
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{modelfetch_error(descriptorPath, "request append", status, rawError)};

    modelfetch_outcome_list_t *rawOutcomes = nullptr;
    rawError = nullptr;
    ModelfetchProgressBridge progressBridge{&loadContext};
    const bool observeProgress =
        loadContext.stopToken.stop_possible() || static_cast<bool>(loadContext.progress);
    status = modelfetch_service_download_asset_requests(service.get(),
                                                        requests.get(),
                                                        observeProgress ? report_modelfetch_progress
                                                                        : nullptr,
                                                        observeProgress ? &progressBridge : nullptr,
                                                        &rawOutcomes,
                                                        &rawError);
    if (status == MODELFETCH_STATUS_CALLBACK_ABORTED) {
        ModelfetchOutcomeList abortedOutcomes(rawOutcomes, modelfetch_outcome_list_free);
        if (progressBridge.callbackFailed.load(std::memory_order_relaxed)) {
            modelfetch_error_free(rawError);
            return tl::unexpected{PEK_ERROR(
                pek::ErrorFlag::SystemFailure,
                fmt::format("Model load progress callback failed for ModelDescriptor [{}]",
                            descriptorPath))};
        }
        if (loadContext.stopRequested()) {
            modelfetch_error_free(rawError);
            return tl::unexpected{model_load_cancelled(descriptorPath)};
        }
        return tl::unexpected{
            modelfetch_error(descriptorPath, "asset download callback", status, rawError)};
    }
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{modelfetch_error(descriptorPath, "asset download", status, rawError)};
    ModelfetchOutcomeList outcomes(rawOutcomes, modelfetch_outcome_list_free);

    if (loadContext.stopRequested())
        return tl::unexpected{model_load_cancelled(descriptorPath)};

    size_t outcomeCount = 0;
    status = modelfetch_outcome_list_count(outcomes.get(), &outcomeCount);
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{modelfetch_error(descriptorPath, "outcome-count lookup", status)};
    if (outcomeCount != 1) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("modelfetch returned {} outcomes for ModelDescriptor [{}]",
                                  outcomeCount,
                                  descriptorPath))};
    }

    const modelfetch_outcome_t *outcome = nullptr;
    status = modelfetch_outcome_list_get(outcomes.get(), 0, &outcome);
    if (status != MODELFETCH_STATUS_OK || outcome == nullptr)
        return tl::unexpected{modelfetch_error(descriptorPath, "outcome lookup", status)};

    modelfetch_text_view_t assetView{nullptr, 0};
    status = modelfetch_outcome_asset_id(outcome, &assetView);
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{modelfetch_error(descriptorPath, "outcome asset lookup", status)};
    auto returnedAssetId = read_text(descriptorPath, "outcome asset ID", assetView);
    if (!returnedAssetId)
        return tl::unexpected{returnedAssetId.error()};
    if (*returnedAssetId != assetId) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("modelfetch returned a mismatched asset for ModelDescriptor [{}]",
                                  descriptorPath))};
    }

    modelfetch_outcome_kind_t outcomeKind = MODELFETCH_OUTCOME_FAILURE;
    status = modelfetch_outcome_kind(outcome, &outcomeKind);
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{modelfetch_error(descriptorPath, "outcome-kind lookup", status)};
    if (outcomeKind == MODELFETCH_OUTCOME_FAILURE) {
        modelfetch_failure_reason_t reason = MODELFETCH_FAILURE_DOWNLOAD_FAILED;
        status = modelfetch_outcome_failure_reason(outcome, &reason);
        const char *detail =
            status == MODELFETCH_STATUS_OK ? failure_reason_name(reason) : "unknown";
        return tl::unexpected{PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format("modelfetch could not materialize modelFile in ModelDescriptor [{}]: {}",
                        descriptorPath,
                        detail))};
    }
    if (outcomeKind != MODELFETCH_OUTCOME_SUCCESS) {
        return tl::unexpected{PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format("modelfetch returned an invalid outcome kind for ModelDescriptor [{}]",
                        descriptorPath))};
    }

    modelfetch_success_status_t successStatus = MODELFETCH_SUCCESS_DOWNLOADED;
    status = modelfetch_outcome_success_status(outcome, &successStatus);
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{modelfetch_error(descriptorPath, "success-status lookup", status)};
    if (successStatus != MODELFETCH_SUCCESS_DOWNLOADED &&
        successStatus != MODELFETCH_SUCCESS_EXISTING) {
        return tl::unexpected{PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format("modelfetch returned an invalid success status for ModelDescriptor [{}]",
                        descriptorPath))};
    }

    size_t pathCount = 0;
    status = modelfetch_outcome_success_path_count(outcome, &pathCount);
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{modelfetch_error(descriptorPath, "model-path count lookup", status)};
    if (pathCount != 1) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("modelfetch returned {} model paths for ModelDescriptor [{}]",
                                  pathCount,
                                  descriptorPath))};
    }

    modelfetch_text_view_t pathView{nullptr, 0};
    status = modelfetch_outcome_success_path(outcome, 0, &pathView);
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{modelfetch_error(descriptorPath, "model-path lookup", status)};
    auto modelPath = read_text(descriptorPath, "model path", pathView);
    if (!modelPath)
        return tl::unexpected{modelPath.error()};

    const modelfetch_integrity_t *integrity = nullptr;
    status = modelfetch_outcome_success_integrity(outcome, 0, &integrity);
    if (status != MODELFETCH_STATUS_OK || integrity == nullptr)
        return tl::unexpected{modelfetch_error(descriptorPath, "integrity lookup", status)};
    modelfetch_text_view_t integrityView{nullptr, 0};
    status = modelfetch_integrity_token(integrity, &integrityView);
    if (status != MODELFETCH_STATUS_OK)
        return tl::unexpected{modelfetch_error(descriptorPath, "integrity-token lookup", status)};
    auto integrityToken = read_text(descriptorPath, "integrity token", integrityView);
    if (!integrityToken)
        return tl::unexpected{integrityToken.error()};
    if (!has_canonical_integrity(*integrityToken)) {
        return tl::unexpected{
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("modelfetch returned invalid integrity for ModelDescriptor [{}]",
                                  descriptorPath))};
    }

    return std_fs::path(*modelPath);
}

pek::Result<std::string> resolveModelFile(const std::string &descriptorPath,
                                          const ModelDescriptor &descriptor,
                                          const pek::ModelLoadContext &loadContext) {
    const bool published = descriptor.modelFile.rfind("hf:", 0) == 0;
    std::error_code ec;
    if (published) {
        auto materializedModel =
            materializePublishedModel(descriptorPath, descriptor.modelFile, loadContext);
        if (!materializedModel)
            return tl::unexpected{materializedModel.error()};
        if (!materializedModel->is_absolute()) {
            return tl::unexpected{PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format("modelfetch returned a relative path for ModelDescriptor [{}]",
                            descriptorPath))};
        }

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
            !is_descendant(modelFile, modelStoreRoot) || !std_fs::is_regular_file(modelFile, ec) ||
            ec) {
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

pek::Result<ModelDescriptor> ModelDescriptor::fromFile(const std::string &path,
                                                       const pek::ModelLoadContext &loadContext) {
    if (loadContext.stopRequested())
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

    if (loadContext.stopRequested())
        return tl::unexpected{model_load_cancelled(path)};

    auto resolvedModelFile = resolveModelFile(path, *descriptor, loadContext);
    if (!resolvedModelFile)
        return tl::unexpected{resolvedModelFile.error()};
    descriptor->modelFile = *resolvedModelFile;
    return descriptor;
}
