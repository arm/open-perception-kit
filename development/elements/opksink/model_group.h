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

#include "model.h"

#include <cstddef>
#include <gst/gst.h>
#include <memory>
#include <string>
#include <vector>

// A model group is a chain of directly connected opkinfer elements.
// Any intervening element of another type separates groups.
class ModelGroup {
  public:
    ModelGroup() = default;
    explicit ModelGroup(Model model);
    ModelGroup(std::string id,
               std::shared_ptr<GstPad> modelGroupInputSourcePad,
               std::shared_ptr<GstPad> modelGroupOutputSinkPad,
               std::vector<Model> models);
    const std::string &getId() const;
    const std::vector<Model> &getModels() const;
    Model *findModel(const std::string &elementName);
    bool isEditable() const;
    bool isConnected() const;
    std::shared_ptr<GstElement> getParentBinForEdit() const;
    void refreshRuntimeDataFrom(const std::vector<ModelGroup> &modelGroups,
                                GstElement *const replacedElement);

    // The caller validates and serializes edits; these operations may emit GStreamer callbacks.
    std::string addModel(Model model, GstElement *const parent, const std::size_t position);
    void replaceModel(const std::size_t position,
                      const std::string &path,
                      const OpChainMetadata &metadata);
    std::string removeModel(const std::size_t position, GstElement *const parent);
    std::string reorderModel(const std::size_t sourcePosition, const std::size_t destination);

  private:
    bool hasEditableBoundaries() const;
    void unlinkModels() const;
    std::string linkModels() const;
    std::string m_id;
    std::shared_ptr<GstPad> m_modelGroupInputSourcePad;
    std::shared_ptr<GstPad> m_modelGroupOutputSinkPad;
    std::vector<Model> m_models;
};
