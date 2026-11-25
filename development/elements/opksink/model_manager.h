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

#include "model_group.h"

#include <cstddef>
#include <cstdint>
#include <gst/gst.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// A temporary copy pins elements and pairs their order with the matching configuration ID.
struct ModelGroupsSnapshot {
    std::vector<ModelGroup> m_modelGroups;
    std::uint64_t m_configurationId;
};

enum class ChangeAction { Add, Replace, Remove, Move };

struct ChangeRequest {
    ChangeAction m_action;
    std::uint64_t m_configurationId;
    std::string m_modelGroup;
    std::string m_elementName;
    std::string m_path;
    std::optional<std::size_t> m_position;
};

class ModelManager {
  public:
    explicit ModelManager(GstElement *const sink);
    ModelGroupsSnapshot getModelGroups() const;
    std::map<std::string, OpChainMetadata> getAvailableModels() const;
    void refreshAvailableModels();

    void registerModel(const ModelData &modelData);

    // The caller serializes commands and holds the pipeline's state-transition lock.
    void edit(GstElement *const pipeline, const ChangeRequest &request);

  private:
    struct PreparedChange;

    mutable std::mutex m_modelChangeMutex;
    std::vector<ModelGroup> m_modelGroups;
    std::map<std::string, OpChainMetadata> m_availableModels;
    std::uint64_t m_configurationId = 0;
    std::uint64_t m_nextModelId = 0;

    static std::vector<ModelGroup> discoverInitialModelGroups(GstElement *const sink);
    static Model readConfiguredModel(const std::shared_ptr<GstElement> &element);
    Model *findModel(const std::string &elementName);
    ModelGroup &findModelGroup(const std::string &modelGroupId);
    OpChainMetadata readAvailableModelMetadata(const std::string &path) const;
    PreparedChange prepareChange(const ModelGroup &modelGroup, const ChangeRequest &request) const;
    std::string
    applyChange(GstElement *const pipeline, ModelGroup &editedGroup, const PreparedChange &request);

    std::string
    addModel(GstElement *const pipeline, ModelGroup &modelGroup, const PreparedChange &request);
    std::shared_ptr<GstElement> createModel(GstElement *const pipeline, const std::string &path);
};
