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

#pragma once

#include "opk/OutputCapability.h"

#include <gst/gst.h>
#include <memory>
#include <string>
#include <vector>

struct OpChainMetadata {
    std::string m_name;
    std::string m_displayName;
    std::string m_task;
    std::string m_runtime;
};

struct ModelInterface {
    std::vector<opk::InputBinding> m_requiredInputs;
    std::vector<opk::OutputReference> m_providedOutputs;
    // Only providers upstream of this model along connected data-flow paths.
    std::vector<opk::OutputReference> m_upstreamInputProviders;
};

// A notification payload, not a separately retained model store.
struct ModelData {
    std::string m_elementName;
    OpChainMetadata m_metadata;
    ModelInterface m_interface;
    bool m_active = false;
};

class Model {
  public:
    explicit Model(const ModelData &modelData);
    Model(std::shared_ptr<GstElement> element, OpChainMetadata metadata);
    const std::string &getElementName() const;
    const std::shared_ptr<GstElement> &getElement() const;
    const OpChainMetadata &getMetadata() const;
    const ModelInterface &getInterface() const;
    bool isConfigured() const;
    bool isActive() const;
    std::string getOpChainPath() const;
    std::vector<opk::InputBinding> getRequiredInputs() const;
    void updateModelData(const ModelData &modelData);
    void replaceOpChain(const std::string &path, const OpChainMetadata &metadata);
    void refreshRuntimeDataFrom(const Model &model);

  private:
    std::string m_elementName;
    std::shared_ptr<GstElement> m_element;
    OpChainMetadata m_metadata;
    ModelInterface m_interface;
    // Used only by registrations without a configured GStreamer element.
    bool m_registeredActive = false;
};
