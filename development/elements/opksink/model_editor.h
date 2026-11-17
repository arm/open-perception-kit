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

#include <cstdint>
#include <gst/gst.h>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class ModelRegistry;

class ModelEditor {
  public:
    explicit ModelEditor(GstElement *const sink);
    void refreshAvailableOpChains();
    nlohmann::json statusOf(const nlohmann::json &registeredModels) const;
    // The caller serializes commands and holds the pipeline's state-transition lock.
    void edit(GstElement *const pipeline, const nlohmann::json &message, ModelRegistry &registry);

  private:
    struct ModelGroup {
        std::string m_id;
        std::shared_ptr<GstPad> m_upstream;
        std::shared_ptr<GstPad> m_downstream{};
        std::vector<std::shared_ptr<GstElement>> m_models{};
    };

    mutable std::mutex m_modelsMutex;
    // A model group is a chain of directly connected opkinfer elements.
    // Any intervening element of another type separates groups.
    std::vector<ModelGroup> m_modelGroups;
    nlohmann::json m_modelMetadataCollection = nlohmann::json::object();
    nlohmann::json m_availableOpChains = nlohmann::json::array();
    std::vector<std::string> m_initialPaths;
    std::uint64_t m_revision = 0;
    std::uint64_t m_nextModelId = 0;

    static void unlinkModelGroup(const ModelGroup &modelGroup);
    static std::string linkModelGroup(const ModelGroup &modelGroup);
    static bool modelGroupIsLinked(const ModelGroup &modelGroup);
    std::shared_ptr<GstElement> createModel(GstElement *const pipeline, const std::string &path);
};
