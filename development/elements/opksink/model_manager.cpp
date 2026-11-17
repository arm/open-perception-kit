/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "model_manager.h"

#include "op/OpChainDescriptor.h"
#include "opk/ModelDescriptor.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
namespace fs = std::filesystem;

bool isModel(GstElement *const element) {
    auto *const factory = element ? gst_element_get_factory(element) : nullptr;
    return factory &&
           g_strcmp0(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)), "opkinfer") == 0;
}

std::string chainPath(GstElement *const element) {
    g_autofree gchar *path = nullptr;
    g_object_get(element, "opchain-path", &path, nullptr);
    return path ? path : "";
}

std::shared_ptr<GstPad> peerPad(GstElement *const element, const char *const padName) {
    g_autoptr(GstPad) pad = gst_element_get_static_pad(element, padName);
    return {pad ? gst_pad_get_peer(pad) : nullptr, gst_object_unref};
}

OpChainMetadata extractModelMetadata(const opk::op::OpChainDescriptor &descriptor) {
    return {descriptor.name, descriptor.displayName, descriptor.task, descriptor.runtime};
}

bool hasInstalledModels(const opk::op::OpChainDescriptor &descriptor) {
    bool hasInference = false;
    for (const auto &operation : descriptor.ops) {
        if (!opk::op::isInferenceOpId(operation.id)) {
            continue;
        }
        hasInference = true;
        const auto model =
            opk::ModelDescriptor::fromFile(operation.attributes.getString("modelDescriptor"));
        std::error_code error;
        if (!model || !fs::is_regular_file(model->modelFile, error)) {
            return false;
        }
    }
    return hasInference;
}

void collectModels(GstElement *const element,
                   std::set<GstElement *> &visitedPipelineElements,
                   std::vector<std::shared_ptr<GstElement>> &pipelineModelElements) {
    if (!visitedPipelineElements.insert(element).second) {
        return;
    }
    g_autoptr(GstIterator) iterator = gst_element_iterate_sink_pads(element);
    GValue item = G_VALUE_INIT;
    auto iteration = gst_iterator_next(iterator, &item);
    while (iteration == GST_ITERATOR_OK) {
        g_autoptr(GstPad) source = gst_pad_get_peer(GST_PAD(g_value_get_object(&item)));
        g_value_reset(&item);
        // Follow ghost pads across bin boundaries without including other sink branches.
        while (source && GST_IS_PROXY_PAD(source)) {
            GstPad *next = nullptr;
            if (GST_IS_GHOST_PAD(source)) {
                next = gst_ghost_pad_get_target(GST_GHOST_PAD(source));
            } else {
                g_autoptr(GstProxyPad) external = gst_proxy_pad_get_internal(GST_PROXY_PAD(source));
                next = external ? gst_pad_get_peer(GST_PAD(external)) : nullptr;
            }
            gst_clear_object(&source);
            source = next;
        }
        g_autoptr(GstElement) upstream = source ? gst_pad_get_parent_element(source) : nullptr;
        if (upstream) {
            collectModels(upstream, visitedPipelineElements, pipelineModelElements);
        }
        iteration = gst_iterator_next(iterator, &item);
    }
    if (G_VALUE_TYPE(&item)) {
        g_value_unset(&item);
    }
    if (iteration != GST_ITERATOR_DONE) {
        throw std::runtime_error("Cannot read a stable pipeline topology");
    }
    if (isModel(element)) {
        pipelineModelElements.emplace_back(GST_ELEMENT(gst_object_ref(element)), gst_object_unref);
    }
}

void ensureStoppedPipeline(GstElement *const pipeline) {
    GstState current = GST_STATE_NULL;
    GstState pending = GST_STATE_VOID_PENDING;
    gst_element_get_state(pipeline, &current, &pending, 0);
    const bool canEditModels = current == GST_STATE_READY && pending == GST_STATE_VOID_PENDING;
    if (!canEditModels) {
        throw std::runtime_error("Stop the pipeline before editing models");
    }
}

} // namespace

struct ModelManager::PreparedChange {
    ChangeAction m_action;
    std::string m_modelGroupId;
    std::size_t m_sourcePosition;
    std::size_t m_destination;
    std::shared_ptr<GstElement> m_parent;
    std::string m_opChainPath;
    OpChainMetadata m_metadata;
};

ModelManager::ModelManager(GstElement *const sink)
    : m_modelGroups(discoverInitialModelGroups(sink)) {}

Model ModelManager::readConfiguredModel(const std::shared_ptr<GstElement> &element) {
    OpChainMetadata metadata{GST_OBJECT_NAME(element.get()), {}, {}, {}};
    const auto path = chainPath(element.get());
    if (!path.empty()) {
        if (const auto descriptor = opk::op::OpChainDescriptor::fromFile(path)) {
            metadata = extractModelMetadata(*descriptor);
        }
    }
    return Model(element, std::move(metadata));
}

std::vector<ModelGroup> ModelManager::discoverInitialModelGroups(GstElement *const sink) {
    std::vector<std::shared_ptr<GstElement>> pipelineModelElements;
    std::set<GstElement *> visitedPipelineElements;
    collectModels(sink, visitedPipelineElements, pipelineModelElements);
    std::set<GstElement *> modelElementsAssignedToGroups;
    std::vector<ModelGroup> modelGroups;
    for (const auto &first : pipelineModelElements) {
        if (modelElementsAssignedToGroups.contains(first.get())) {
            continue;
        }
        const auto inputSourcePad = peerPad(first.get(), "sink");
        std::shared_ptr<GstPad> outputSinkPad;
        std::vector<Model> groupModels;
        auto element = first;
        while (element && modelElementsAssignedToGroups.insert(element.get()).second) {
            groupModels.push_back(readConfiguredModel(element));
            outputSinkPad = peerPad(element.get(), "src");
            g_autoptr(GstElement) next =
                outputSinkPad ? gst_pad_get_parent_element(outputSinkPad.get()) : nullptr;
            const auto found =
                std::ranges::find_if(pipelineModelElements, [next](const auto &candidate) {
                    return candidate.get() == next;
                });
            element = found != pipelineModelElements.end() ? *found : nullptr;
        }
        modelGroups.emplace_back("model-group-" + std::to_string(modelGroups.size()),
                                 inputSourcePad,
                                 std::move(outputSinkPad),
                                 std::move(groupModels));
    }
    return modelGroups;
}

ModelGroupsSnapshot ModelManager::getModelGroups() const {
    std::scoped_lock lock(m_modelChangeMutex);
    return {m_modelGroups, m_configurationId};
}

std::map<std::string, OpChainMetadata> ModelManager::getAvailableModels() const {
    std::scoped_lock lock(m_modelChangeMutex);
    return m_availableModels;
}

void ModelManager::refreshAvailableModels() {
    const char *const configuredRoot = g_getenv("OPK_PROJECT_ROOT");
    const fs::path root =
        configuredRoot && *configuredRoot ? fs::path(configuredRoot) : fs::current_path();
    std::map<std::string, OpChainMetadata> refreshedModels;
    std::error_code error;
    fs::recursive_directory_iterator entry(
        root / "config" / "models", fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    for (; entry != end && !error; entry.increment(error)) {
        if (entry->path().filename() != "opchain.json" || !entry->is_regular_file(error)) {
            continue;
        }
        const auto path = fs::absolute(entry->path(), error);
        if (error) {
            continue;
        }
        const auto descriptor = opk::op::OpChainDescriptor::fromFile(path.string());
        if (descriptor && hasInstalledModels(*descriptor)) {
            refreshedModels.emplace(path.string(), extractModelMetadata(*descriptor));
        }
    }
    std::scoped_lock lock(m_modelChangeMutex);
    m_availableModels = std::move(refreshedModels);
}

Model *ModelManager::findModel(const std::string &elementName) {
    for (auto &group : m_modelGroups) {
        if (auto *const model = group.findModel(elementName)) {
            return model;
        }
    }
    return nullptr;
}

void ModelManager::registerModel(const ModelData &modelData) {
    std::scoped_lock lock(m_modelChangeMutex);
    auto *const model = findModel(modelData.m_elementName);
    if (!model) {
        m_modelGroups.emplace_back(Model(modelData));
        return;
    }
    model->updateModelData(modelData);
}

std::shared_ptr<GstElement> ModelManager::createModel(GstElement *const pipeline,
                                                      const std::string &path) {
    std::string name;
    do {
        name = "opkinfer-web-" + std::to_string(++m_nextModelId);
        g_autoptr(GstElement) existing = gst_bin_get_by_name(GST_BIN(pipeline), name.c_str());
        if (!existing) {
            break;
        }
    } while (true);
    auto *const element = gst_element_factory_make("opkinfer", name.c_str());
    if (!element) {
        throw std::runtime_error("Cannot create an inference element");
    }
    std::shared_ptr<GstElement> model(GST_ELEMENT(gst_object_ref_sink(element)), gst_object_unref);
    g_object_set(element, "opchain-path", path.c_str(), nullptr);
    return model;
}

ModelGroup &ModelManager::findModelGroup(const std::string &modelGroupId) {
    const auto found = std::ranges::find_if(m_modelGroups, [&modelGroupId](const auto &group) {
        return group.isEditable() && group.getId() == modelGroupId;
    });
    if (found == m_modelGroups.end()) {
        throw std::runtime_error("Unknown model group");
    }
    return *found;
}

OpChainMetadata ModelManager::readAvailableModelMetadata(const std::string &path) const {
    if (!m_availableModels.contains(path)) {
        throw std::runtime_error("Select an available server model");
    }
    const auto descriptor = opk::op::OpChainDescriptor::fromFile(path);
    if (!descriptor || !hasInstalledModels(*descriptor)) {
        throw std::runtime_error("OpChain or model files changed; refresh the available models");
    }
    return extractModelMetadata(*descriptor);
}

ModelManager::PreparedChange ModelManager::prepareChange(const ModelGroup &modelGroup,
                                                         const ChangeRequest &request) const {
    const auto position =
        std::ranges::find(modelGroup.getModels(), request.m_elementName, &Model::getElementName);
    if (request.m_action != ChangeAction::Add && position == modelGroup.getModels().end()) {
        throw std::runtime_error("Model is no longer in this model group");
    }
    const bool newModelLoaded =
        request.m_action == ChangeAction::Add || request.m_action == ChangeAction::Replace;
    const auto metadata =
        newModelLoaded ? readAvailableModelMetadata(request.m_path) : OpChainMetadata{};
    const auto destination = request.m_position.value_or(modelGroup.getModels().size());
    const bool positionOutsideGroup =
        destination > modelGroup.getModels().size() ||
        (request.m_action == ChangeAction::Move && destination == modelGroup.getModels().size());
    if (positionOutsideGroup) {
        throw std::runtime_error("Model position is outside this model group");
    }
    return {request.m_action,
            modelGroup.getId(),
            static_cast<std::size_t>(position - modelGroup.getModels().begin()),
            destination,
            modelGroup.getParentBinForEdit(),
            request.m_path,
            metadata};
}

std::string ModelManager::addModel(GstElement *const pipeline,
                                   ModelGroup &modelGroup,
                                   const PreparedChange &request) {
    const auto element = createModel(pipeline, request.m_opChainPath);
    return modelGroup.addModel(
        Model(element, request.m_metadata), request.m_parent.get(), request.m_destination);
}

std::string ModelManager::applyChange(GstElement *const pipeline,
                                      ModelGroup &editedGroup,
                                      const PreparedChange &request) {
    std::string error;
    std::shared_ptr<GstElement> replacedElement;
    try {
        switch (request.m_action) {
        case ChangeAction::Add:
            error = addModel(pipeline, editedGroup, request);
            break;
        case ChangeAction::Replace:
            replacedElement = editedGroup.getModels().at(request.m_sourcePosition).getElement();
            editedGroup.replaceModel(
                request.m_sourcePosition, request.m_opChainPath, request.m_metadata);
            break;
        case ChangeAction::Remove:
            error = editedGroup.removeModel(request.m_sourcePosition, request.m_parent.get());
            break;
        case ChangeAction::Move:
            error = editedGroup.reorderModel(request.m_sourcePosition, request.m_destination);
            break;
        default:
            error = "Unknown model edit";
            break;
        }
    } catch (const std::exception &failure) {
        error = failure.what();
    }

    std::scoped_lock lock(m_modelChangeMutex);
    auto &currentGroup = findModelGroup(request.m_modelGroupId);
    // Runtime notifications may have arrived while the data lock was released.
    editedGroup.refreshRuntimeDataFrom(m_modelGroups, replacedElement.get());
    // Retired elements stay pinned until edit() exits outside the data lock.
    std::swap(currentGroup, editedGroup);
    ++m_configurationId;
    return error;
}

void ModelManager::edit(GstElement *const pipeline, const ChangeRequest &request) {
    ensureStoppedPipeline(pipeline);
    ModelGroup editedGroup;
    std::unique_lock lock(m_modelChangeMutex);
    if (request.m_configurationId != m_configurationId) {
        throw std::runtime_error(
            "The model list changed in another client; retry with the current order");
    }
    editedGroup = findModelGroup(request.m_modelGroup);
    const auto preparedChange = prepareChange(editedGroup, request);
    // The caller retains command serialization and state locks. GStreamer callbacks
    // may stop the server and join a reporting thread that needs this data mutex.
    lock.unlock();

    const auto error = applyChange(pipeline, editedGroup, preparedChange);
    if (!error.empty()) {
        throw std::runtime_error(error);
    }
}
