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

#include "model_reg.h"

#include <cstdint>
#include <gst/gst.h>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class ModelEditor {
  public:
    ModelEditor(GstElement *const sink, ModelRegistry &registry);
    void refreshAvailableOpChains();
    nlohmann::json statusOf(const ModelRegistry &registeredModels) const;
    // The caller serializes commands and holds the pipeline's state-transition lock.
    void edit(GstElement *const pipeline, const nlohmann::json &message, ModelRegistry &registry);

  private:
    struct ModelGroup {
        std::string m_id;
        std::shared_ptr<GstPad> m_upstream;
        std::shared_ptr<GstPad> m_downstream{};
        std::vector<std::shared_ptr<GstElement>> m_models{};
    };

    struct PreparedModelEdit;

    mutable std::mutex m_modelsMutex;
    // A model group is a chain of directly connected opkinfer elements.
    // Any intervening element of another type separates groups.
    std::vector<ModelGroup> m_modelGroups;
    std::map<std::string, ModelStatus> m_availableOpChains;
    std::vector<std::string> m_initialPaths;
    // Configuration ID is used to ensure consistency when the model chain is edited in multiple
    // browsers.
    std::uint64_t m_configurationId = 0;
    // Next model ID is used to generate unique names for opkinfer elements added through the Web
    // UI.
    std::uint64_t m_nextModelId = 0;

    static ModelStatus getModelStatus(GstElement *const element,
                                      const std::string &modelGroupId,
                                      const bool connected,
                                      ModelStatus modelStatus);
    nlohmann::json getAvailableOpChainsJson() const;

    ModelGroup &findEditedModelGroup(const nlohmann::json &message);
    ModelStatus metadataForAvailableOpChain(const std::string &path) const;
    static std::shared_ptr<GstElement> validateModelGroupMembership(const ModelGroup &modelGroup);
    PreparedModelEdit prepareModelEdit(const ModelGroup &modelGroup,
                                       const nlohmann::json &message) const;
    std::string applyModelEdit(GstElement *const pipeline,
                               ModelGroup &modelGroup,
                               const PreparedModelEdit &request);
    static void publishModelEdit(ModelGroup &currentGroup,
                                 ModelGroup &editedGroup,
                                 PreparedModelEdit &request,
                                 ModelRegistry &registry);
    static void finishModelEdit(const PreparedModelEdit &request,
                                ModelRegistry &registry,
                                const std::string &error);

    static void unlinkModelGroup(const ModelGroup &modelGroup);
    static std::string linkModelGroup(const ModelGroup &modelGroup);
    static bool modelGroupIsLinked(const ModelGroup &modelGroup);
    std::shared_ptr<GstElement> createModel(GstElement *const pipeline, const std::string &path);
};
