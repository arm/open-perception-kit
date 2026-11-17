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

#include "model_editor.h"

#include "model_reg.h"
#include "op/OpChainDescriptor.h"
#include "opk/ModelDescriptor.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

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

json extractModelMetadata(const opk::op::OpChainDescriptor &descriptor) {
    return {{"name", descriptor.name},
            {"displayName", descriptor.displayName},
            {"task", descriptor.task},
            {"runtime", descriptor.runtime}};
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

void addCandidate(const fs::path &path, std::set<fs::path> &candidates) {
    const auto filename = path.filename().string();
    const bool isOpChainFile = filename == "opchain.json" ||
                               (filename.starts_with("opchain-") && filename.ends_with(".json"));
    std::error_code error;
    if (isOpChainFile && fs::is_regular_file(path, error)) {
        const auto resolved = fs::absolute(path, error);
        if (!error) {
            candidates.insert(resolved);
        }
    }
}

} // namespace

ModelEditor::ModelEditor(GstElement *const sink) {
    std::vector<std::shared_ptr<GstElement>> pipelineModelElements;
    std::set<GstElement *> visitedPipelineElements;
    collectModels(sink, visitedPipelineElements, pipelineModelElements);
    // A model group is a chain of directly connected opkinfer elements.
    // Any intervening element of another type separates groups.
    std::set<GstElement *> modelElementsAssignedToGroups;
    for (const auto &model : pipelineModelElements) {
        const auto opchainPath = chainPath(model.get());
        if (!opchainPath.empty()) {
            m_initialPaths.push_back(fs::absolute(opchainPath).string());
            if (const auto descriptor = opk::op::OpChainDescriptor::fromFile(opchainPath)) {
                m_modelMetadataCollection[GST_OBJECT_NAME(model.get())] =
                    extractModelMetadata(*descriptor);
            }
        }
    }
    for (const auto &first : pipelineModelElements) {
        if (modelElementsAssignedToGroups.contains(first.get())) {
            continue;
        }
        ModelGroup modelGroup{.m_id = GST_OBJECT_NAME(first.get()),
                              .m_upstream = peerPad(first.get(), "sink")};
        auto model = first;
        while (model && !modelElementsAssignedToGroups.contains(model.get())) {
            modelElementsAssignedToGroups.insert(model.get());
            modelGroup.m_models.push_back(model);
            modelGroup.m_downstream = peerPad(model.get(), "src");
            g_autoptr(GstElement) next =
                modelGroup.m_downstream ? gst_pad_get_parent_element(modelGroup.m_downstream.get())
                                        : nullptr;
            const auto found =
                std::find_if(pipelineModelElements.begin(),
                             pipelineModelElements.end(),
                             [next](const auto &candidate) { return candidate.get() == next; });
            model = found != pipelineModelElements.end() ? *found : nullptr;
        }
        g_autoptr(GstElement) upstream =
            modelGroup.m_upstream ? gst_pad_get_parent_element(modelGroup.m_upstream.get())
                                  : nullptr;
        g_autoptr(GstElement) downstream =
            modelGroup.m_downstream ? gst_pad_get_parent_element(modelGroup.m_downstream.get())
                                    : nullptr;
        g_autoptr(GstObject) parent = gst_object_get_parent(GST_OBJECT(first.get()));
        const bool hasFixedBoundaries = upstream && downstream && parent &&
                                        gst_object_has_as_parent(GST_OBJECT(upstream), parent) &&
                                        gst_object_has_as_parent(GST_OBJECT(downstream), parent);
        if (!hasFixedBoundaries) {
            modelGroup.m_id.clear();
        }
        m_modelGroups.push_back(std::move(modelGroup));
    }
}

void ModelEditor::refreshAvailableOpChains() {
    std::set<fs::path> candidates;
    const char *const configuredRoot = std::getenv("OPK_PROJECT_ROOT");
    const fs::path root =
        configuredRoot && *configuredRoot ? fs::path(configuredRoot) : fs::current_path();
    for (const auto *const directory : {"models", "opchains"}) {
        std::error_code error;
        fs::recursive_directory_iterator entry(
            root / "config" / directory, fs::directory_options::skip_permission_denied, error);
        const fs::recursive_directory_iterator end;
        for (; entry != end && !error; entry.increment(error)) {
            addCandidate(entry->path(), candidates);
        }
    }
    for (const auto &path : m_initialPaths) {
        std::error_code error;
        const auto resolved = fs::absolute(path, error);
        if (!error) {
            candidates.insert(resolved);
        }
        error.clear();
        fs::directory_iterator entry(
            fs::path(path).parent_path(), fs::directory_options::skip_permission_denied, error);
        const fs::directory_iterator end;
        for (; entry != end && !error; entry.increment(error)) {
            addCandidate(entry->path(), candidates);
        }
    }
    auto availableOpChains = json::array();
    for (const auto &path : candidates) {
        const auto descriptor = opk::op::OpChainDescriptor::fromFile(path.string());
        if (!descriptor || !hasInstalledModels(*descriptor)) {
            continue;
        }
        auto entry = extractModelMetadata(*descriptor);
        entry["path"] = path.string();
        availableOpChains.push_back(std::move(entry));
    }
    std::scoped_lock lock(m_modelsMutex);
    m_availableOpChains = std::move(availableOpChains);
}

void ModelEditor::unlinkModelGroup(const ModelGroup &modelGroup) {
    const auto unlinkSource = [](GstPad *const source) {
        g_autoptr(GstPad) peer = gst_pad_get_peer(source);
        if (peer) {
            gst_pad_unlink(source, peer);
        }
    };
    unlinkSource(modelGroup.m_upstream.get());
    for (const auto &model : modelGroup.m_models) {
        g_autoptr(GstPad) source = gst_element_get_static_pad(model.get(), "src");
        unlinkSource(source);
    }
}

std::string ModelEditor::linkModelGroup(const ModelGroup &modelGroup) {
    GstPad *source = modelGroup.m_upstream.get();
    g_autoptr(GstPad) previousSource = nullptr;
    std::string error;
    const auto link = [&error](GstPad *const from, GstPad *const to) {
        const auto result = gst_pad_link(from, to);
        if (result != GST_PAD_LINK_OK && error.empty()) {
            error = std::string("Cannot link models: ") + gst_pad_link_get_name(result);
        }
    };
    for (const auto &model : modelGroup.m_models) {
        g_autoptr(GstPad) sink = gst_element_get_static_pad(model.get(), "sink");
        link(source, sink);
        gst_clear_object(&previousSource);
        previousSource = gst_element_get_static_pad(model.get(), "src");
        source = previousSource;
    }
    link(source, modelGroup.m_downstream.get());
    return error;
}

bool ModelEditor::modelGroupIsLinked(const ModelGroup &modelGroup) {
    g_autoptr(GstPad) expected = gst_pad_get_peer(modelGroup.m_upstream.get());
    for (const auto &model : modelGroup.m_models) {
        g_autoptr(GstPad) sink = gst_element_get_static_pad(model.get(), "sink");
        if (expected != sink) {
            return false;
        }
        gst_clear_object(&expected);
        g_autoptr(GstPad) source = gst_element_get_static_pad(model.get(), "src");
        expected = gst_pad_get_peer(source);
    }
    return expected == modelGroup.m_downstream.get();
}

nlohmann::json ModelEditor::statusOf(const json &registeredModels) const {
    std::scoped_lock lock(m_modelsMutex);
    std::unordered_map<std::string, json> registeredModelsByElementName;
    for (const auto &model : registeredModels) {
        registeredModelsByElementName.emplace(model.at("element_name").get<std::string>(), model);
    }
    auto models = json::array();
    auto modelGroups = json::array();
    const auto appendModel =
        [&](GstElement *const element, const std::string &modelGroupId, const bool connected) {
            const std::string name = GST_OBJECT_NAME(element);
            auto model = registeredModelsByElementName.contains(name)
                             ? registeredModelsByElementName.at(name)
                             : m_modelMetadataCollection.value(name, json::object());
            gboolean active = FALSE;
            g_object_get(element, "active", &active, nullptr);
            model["name"] = model.value("name", name);
            model["element_name"] = name;
            model["active"] = active != FALSE;
            const auto path = chainPath(element);
            model["opchainPath"] = path.empty() ? path : fs::absolute(path).string();
            model["modelGroup"] = modelGroupId;
            model["connected"] = connected;
            models.push_back(std::move(model));
            registeredModelsByElementName.erase(name);
        };
    for (const auto &modelGroup : m_modelGroups) {
        const bool connected = !modelGroup.m_id.empty() && modelGroupIsLinked(modelGroup);
        if (!modelGroup.m_id.empty()) {
            modelGroups.push_back({{"id", modelGroup.m_id}, {"connected", connected}});
        }
        for (const auto &model : modelGroup.m_models) {
            appendModel(model.get(), modelGroup.m_id, connected);
        }
    }
    // Preserve registrations from application-specific inference elements.
    for (const auto &[name, model] : registeredModelsByElementName) {
        models.push_back(model);
    }
    return {{"models", models},
            {"editor",
             {{"modelGroups", modelGroups},
              {"availableOpChains", m_availableOpChains},
              {"revision", m_revision}}}};
}

std::shared_ptr<GstElement> ModelEditor::createModel(GstElement *const pipeline,
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

void ModelEditor::edit(GstElement *const pipeline, const json &message, ModelRegistry &registry) {
    GstState current = GST_STATE_NULL;
    GstState pending = GST_STATE_VOID_PENDING;
    gst_element_get_state(pipeline, &current, &pending, 0);
    const bool canEditModels = current == GST_STATE_READY && pending == GST_STATE_VOID_PENDING;
    if (!canEditModels) {
        throw std::runtime_error("Stop the pipeline before editing models");
    }
    std::unique_lock lock(m_modelsMutex);
    if (!message.at("revision").is_number_unsigned() ||
        message.at("revision").get<std::uint64_t>() != m_revision) {
        throw std::runtime_error(
            "The model list changed in another client; retry with the current order");
    }
    const auto modelGroupId = message.at("modelGroup").get<std::string>();
    const auto modelGroupPosition = std::find_if(
        m_modelGroups.begin(), m_modelGroups.end(), [&modelGroupId](const auto &modelGroup) {
            return !modelGroup.m_id.empty() && modelGroup.m_id == modelGroupId;
        });
    if (modelGroupPosition == m_modelGroups.end()) {
        throw std::runtime_error("Unknown model group");
    }
    auto modelGroup = *modelGroupPosition;
    const auto action = message.at("action").get<std::string>();
    const auto name = message.value("name", std::string{});
    auto position =
        std::find_if(modelGroup.m_models.begin(),
                     modelGroup.m_models.end(),
                     [&name](const auto &model) { return name == GST_OBJECT_NAME(model.get()); });
    if (action != "add" && position == modelGroup.m_models.end()) {
        throw std::runtime_error("Model is no longer in this model group");
    }
    std::string path;
    json modelMetadata;
    if (action == "add" || action == "replace") {
        path = message.at("path").get<std::string>();
        const bool isSelectable =
            std::any_of(m_availableOpChains.begin(),
                        m_availableOpChains.end(),
                        [&path](const auto &entry) { return entry.at("path") == path; });
        if (!isSelectable) {
            throw std::runtime_error("Select an available server OpChain");
        }
        const auto parsed = opk::op::OpChainDescriptor::fromFile(path);
        if (!parsed || !hasInstalledModels(*parsed)) {
            throw std::runtime_error(
                "OpChain or model files changed; refresh the available models");
        }
        modelMetadata = extractModelMetadata(*parsed);
    }
    g_autoptr(GstElement) upstream = gst_pad_get_parent_element(modelGroup.m_upstream.get());
    g_autoptr(GstElement) downstream = gst_pad_get_parent_element(modelGroup.m_downstream.get());
    g_autoptr(GstObject) parent = upstream ? gst_object_get_parent(GST_OBJECT(upstream)) : nullptr;
    const bool groupStillBelongsToPipeline =
        parent && GST_IS_BIN(parent) && downstream &&
        gst_object_has_as_parent(GST_OBJECT(downstream), parent) &&
        std::all_of(
            modelGroup.m_models.begin(), modelGroup.m_models.end(), [parent](const auto &model) {
                return gst_object_has_as_parent(GST_OBJECT(model.get()), parent);
            });
    if (!groupStillBelongsToPipeline) {
        throw std::runtime_error("The model group changed in another controller; restart the "
                                 "application to rediscover models");
    }
    const bool supportedAction =
        action == "replace" || action == "move" || action == "remove" || action == "add";
    if (!supportedAction) {
        throw std::runtime_error("Unknown model edit");
    }
    if (action == "move") {
        if (!message.at("position").is_number_unsigned()) {
            throw std::runtime_error("Model position must be a non-negative integer");
        }
        if (message.at("position").get<std::size_t>() >= modelGroup.m_models.size()) {
            throw std::runtime_error("Model position is outside this model group");
        }
    }
    ++m_revision;
    // GStreamer callbacks may stop the server and join its reporting thread.
    // The command worker and pipeline state lock serialize edits while this lock is released.
    lock.unlock();

    std::string invalidatedModel;
    std::shared_ptr<GstElement> modelToDisconnect;
    std::string error;
    if (action == "replace") {
        modelToDisconnect = *position;
        g_object_set(position->get(), "opchain-path", path.c_str(), nullptr);
        invalidatedModel = name;
    } else if (action == "move") {
        const auto destination = message.at("position").get<std::size_t>();
        unlinkModelGroup(modelGroup);
        auto model = *position;
        modelGroup.m_models.erase(position);
        modelGroup.m_models.insert(modelGroup.m_models.begin() + destination, std::move(model));
        error = linkModelGroup(modelGroup);
    } else if (action == "remove") {
        auto model = *position;
        if (gst_element_set_state(model.get(), GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
            throw std::runtime_error("Cannot stop the selected model");
        }
        unlinkModelGroup(modelGroup);
        if (!gst_bin_remove(GST_BIN(parent), model.get())) {
            throw std::runtime_error("Cannot remove the selected model");
        }
        modelGroup.m_models.erase(position);
        invalidatedModel = name;
        error = linkModelGroup(modelGroup);
    } else if (action == "add") {
        auto model = createModel(pipeline, path);
        if (!gst_bin_add(GST_BIN(parent), model.get())) {
            throw std::runtime_error("Cannot add the selected model");
        }
        unlinkModelGroup(modelGroup);
        modelGroup.m_models.push_back(model);
        error = linkModelGroup(modelGroup);
        if (!gst_element_sync_state_with_parent(model.get()) && error.empty()) {
            error = "Cannot prepare the added model";
        }
    }
    lock.lock();
    if (action == "replace") {
        m_modelMetadataCollection[name] = std::move(modelMetadata);
    } else if (action == "add") {
        m_modelMetadataCollection[GST_OBJECT_NAME(modelGroup.m_models.back().get())] =
            std::move(modelMetadata);
    } else if (action == "remove") {
        m_modelMetadataCollection.erase(name);
    }
    // Release references to removed elements only after unlocking; disposal can emit callbacks.
    std::swap(*modelGroupPosition, modelGroup);
    lock.unlock();
    if (modelToDisconnect) {
        g_object_set(modelToDisconnect.get(), "input-bindings", "[]", nullptr);
    }
    if (!invalidatedModel.empty()) {
        registry.del_model(invalidatedModel);
    }
    if (!error.empty()) {
        throw std::runtime_error(error);
    }
}
