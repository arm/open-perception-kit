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

#include "model.h"

#include "opk/InputBindings.h"

#include <algorithm>
#include <filesystem>
#include <utility>

Model::Model(const ModelData &modelData)
    : m_elementName(modelData.m_elementName), m_metadata(modelData.m_metadata),
      m_interface(modelData.m_interface), m_registeredActive(modelData.m_active) {}

Model::Model(std::shared_ptr<GstElement> element, OpChainMetadata metadata)
    : m_elementName(GST_OBJECT_NAME(element.get())), m_element(std::move(element)),
      m_metadata(std::move(metadata)) {}

const std::shared_ptr<GstElement> &Model::getElement() const {
    return m_element;
}

void Model::updateModelData(const ModelData &modelData) {
    m_metadata = modelData.m_metadata;
    m_interface = modelData.m_interface;
    if (!isConfigured()) {
        m_registeredActive = modelData.m_active;
    }
}

void Model::replaceOpChain(const std::string &path, const OpChainMetadata &metadata) {
    g_object_set(m_element.get(), "opchain-path", path.c_str(), nullptr);
    g_object_set(m_element.get(), "input-bindings", "[]", nullptr);
    m_metadata = metadata;
    m_interface = {};
}

void Model::refreshRuntimeDataFrom(const Model &model) {
    m_metadata = model.m_metadata;
    m_interface = model.m_interface;
}

const std::string &Model::getElementName() const {
    return m_elementName;
}
const OpChainMetadata &Model::getMetadata() const {
    return m_metadata;
}
const ModelInterface &Model::getInterface() const {
    return m_interface;
}
bool Model::isConfigured() const {
    return m_element != nullptr;
}

bool Model::isActive() const {
    if (!m_element) {
        return m_registeredActive;
    }
    gboolean active = FALSE;
    g_object_get(m_element.get(), "active", &active, nullptr);
    return active != FALSE;
}

std::string Model::getOpChainPath() const {
    if (!m_element) {
        return {};
    }
    g_autofree gchar *path = nullptr;
    g_object_get(m_element.get(), "opchain-path", &path, nullptr);
    return path && *path ? std::filesystem::absolute(path).string() : "";
}

std::vector<opk::InputBinding> Model::getRequiredInputs() const {
    auto inputs = m_interface.m_requiredInputs;
    if (!m_element || inputs.empty()) {
        return inputs;
    }
    g_autofree gchar *encodedBindings = nullptr;
    g_object_get(m_element.get(), "input-bindings", &encodedBindings, nullptr);
    const auto bindings = opk::parseInputBindings(encodedBindings);
    if (!bindings) {
        return inputs;
    }
    // Stopped input changes live on the element; an explicit empty producer stays empty.
    for (auto &input : inputs) {
        const auto selection = std::ranges::find_if(*bindings, [&input](const auto &binding) {
            return input.consumerId == binding.consumerId && input.inputId == binding.inputId &&
                   input.payloadType == binding.payloadType;
        });
        if (selection != bindings->end()) {
            input.producerId = selection->producerId;
        }
    }
    return inputs;
}
