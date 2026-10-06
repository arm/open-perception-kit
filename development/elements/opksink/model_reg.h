/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#ifndef __MODEL_REGISTRY_H__
#define __MODEL_REGISTRY_H__

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <gst/gst.h>

#include "status_reporter.h"

struct ModelStatus {
    std::string name;
    bool active = false;
    std::string element_name;
    std::string display_name{};
    std::string task{};
    std::string runtime{};
    std::vector<std::string> provided_content_types{};
    std::vector<std::string> required_content_types{};
};

std::optional<ModelStatus> model_status_from_registration(const GstStructure *structure);

class ModelRegistry : public StatusReporter {
    mutable std::mutex model_registry_mutex;
    std::map<std::string, ModelStatus> model_registry; // key: element_name

  public:
    void add_model(const ModelStatus &status);
    void del_model(const std::string &element_name);

    nlohmann::json report() const override;
};

#endif // !__MODEL_REGISTRY_H__
