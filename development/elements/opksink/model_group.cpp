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

#include "model_group.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

ModelGroup::ModelGroup(Model model) {
    m_models.push_back(std::move(model));
}

ModelGroup::ModelGroup(std::string id,
                       std::shared_ptr<GstPad> modelGroupInputSourcePad,
                       std::shared_ptr<GstPad> modelGroupOutputSinkPad,
                       std::vector<Model> models)
    : m_id(std::move(id)), m_modelGroupInputSourcePad(std::move(modelGroupInputSourcePad)),
      m_modelGroupOutputSinkPad(std::move(modelGroupOutputSinkPad)), m_models(std::move(models)) {
    if (!hasEditableBoundaries()) {
        m_id.clear();
    }
}

Model *ModelGroup::findModel(const std::string &elementName) {
    const auto found = std::ranges::find(m_models, elementName, &Model::getElementName);
    return found != m_models.end() ? &*found : nullptr;
}

void ModelGroup::refreshRuntimeDataFrom(const std::vector<ModelGroup> &modelGroups,
                                        GstElement *const replacedElement) {
    for (auto &model : m_models) {
        if (model.getElement().get() == replacedElement) {
            continue;
        }
        for (const auto &group : modelGroups) {
            const auto current =
                std::ranges::find(group.m_models, model.getElement(), &Model::getElement);
            if (current != group.m_models.end()) {
                model.refreshRuntimeDataFrom(*current);
                break;
            }
        }
    }
}

const std::string &ModelGroup::getId() const {
    return m_id;
}
const std::vector<Model> &ModelGroup::getModels() const {
    return m_models;
}
bool ModelGroup::isEditable() const {
    return !m_id.empty();
}

bool ModelGroup::isConnected() const {
    if (!isEditable()) {
        return false;
    }
    g_autoptr(GstPad) expected = gst_pad_get_peer(m_modelGroupInputSourcePad.get());
    for (const auto &model : m_models) {
        g_autoptr(GstPad) sink = gst_element_get_static_pad(model.getElement().get(), "sink");
        if (expected != sink) {
            return false;
        }
        gst_clear_object(&expected);
        g_autoptr(GstPad) source = gst_element_get_static_pad(model.getElement().get(), "src");
        expected = gst_pad_get_peer(source);
    }
    return expected == m_modelGroupOutputSinkPad.get();
}

bool ModelGroup::hasEditableBoundaries() const {
    if (!m_modelGroupInputSourcePad || !m_modelGroupOutputSinkPad) {
        return false;
    }
    g_autoptr(GstElement) inputSource =
        gst_pad_get_parent_element(m_modelGroupInputSourcePad.get());
    g_autoptr(GstElement) outputSink = gst_pad_get_parent_element(m_modelGroupOutputSinkPad.get());
    g_autoptr(GstObject) parent =
        inputSource ? gst_object_get_parent(GST_OBJECT(inputSource)) : nullptr;
    const bool canEditWithinOneBin =
        parent && GST_IS_BIN(parent) && outputSink &&
        gst_object_has_as_parent(GST_OBJECT(outputSink), parent) &&
        std::ranges::all_of(m_models, [parent](const Model &model) {
            return gst_object_has_as_parent(GST_OBJECT(model.getElement().get()), parent);
        });
    return canEditWithinOneBin;
}

void ModelGroup::unlinkModels() const {
    const auto unlinkSource = [](GstPad *const source) {
        g_autoptr(GstPad) peer = gst_pad_get_peer(source);
        if (peer) {
            gst_pad_unlink(source, peer);
        }
    };
    unlinkSource(m_modelGroupInputSourcePad.get());
    for (const auto &model : m_models) {
        g_autoptr(GstPad) source = gst_element_get_static_pad(model.getElement().get(), "src");
        unlinkSource(source);
    }
}

std::string ModelGroup::linkModels() const {
    GstPad *source = m_modelGroupInputSourcePad.get();
    g_autoptr(GstPad) previousSource = nullptr;
    std::string error;
    const auto link = [&error](GstPad *const from, GstPad *const to) {
        const auto result = gst_pad_link(from, to);
        if (result != GST_PAD_LINK_OK && error.empty()) {
            error = std::string("Cannot link models: ") + gst_pad_link_get_name(result);
        }
    };
    for (const auto &model : m_models) {
        g_autoptr(GstPad) sink = gst_element_get_static_pad(model.getElement().get(), "sink");
        link(source, sink);
        gst_clear_object(&previousSource);
        previousSource = gst_element_get_static_pad(model.getElement().get(), "src");
        source = previousSource;
    }
    link(source, m_modelGroupOutputSinkPad.get());
    return error;
}

std::shared_ptr<GstElement> ModelGroup::getParentBinForEdit() const {
    if (!hasEditableBoundaries()) {
        throw std::runtime_error("The model group changed in another controller; restart the "
                                 "application to rediscover models");
    }
    g_autoptr(GstElement) inputSource =
        gst_pad_get_parent_element(m_modelGroupInputSourcePad.get());
    return {GST_ELEMENT(gst_object_get_parent(GST_OBJECT(inputSource))), gst_object_unref};
}

void ModelGroup::replaceModel(const std::size_t position,
                              const std::string &path,
                              const OpChainMetadata &metadata) {
    m_models.at(position).replaceOpChain(path, metadata);
}

std::string ModelGroup::reorderModel(const std::size_t sourcePosition,
                                     const std::size_t destination) {
    unlinkModels();
    auto model = std::move(m_models.at(sourcePosition));
    m_models.erase(m_models.begin() + sourcePosition);
    m_models.insert(m_models.begin() + destination, std::move(model));
    return linkModels();
}

std::string ModelGroup::removeModel(const std::size_t position, GstElement *const parent) {
    const auto element = m_models.at(position).getElement();
    if (gst_element_set_state(element.get(), GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
        return "Cannot stop the selected model";
    }
    unlinkModels();
    if (!gst_bin_remove(GST_BIN(parent), element.get())) {
        return "Cannot remove the selected model";
    }
    m_models.erase(m_models.begin() + position);
    return linkModels();
}

std::string
ModelGroup::addModel(Model model, GstElement *const parent, const std::size_t position) {
    const auto element = model.getElement();
    // Allocate the record storage before changing the live bin.
    m_models.reserve(m_models.size() + 1);
    if (!gst_bin_add(GST_BIN(parent), element.get())) {
        return "Cannot add the selected model";
    }
    unlinkModels();
    m_models.insert(m_models.begin() + position, std::move(model));
    auto error = linkModels();
    if (!gst_element_sync_state_with_parent(element.get()) && error.empty()) {
        error = "Cannot prepare the added model";
    }
    return error;
}
