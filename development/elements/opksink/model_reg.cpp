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

#include "model_reg.h"

#include "Log.h"
#include "gst/ResultRequirementEvent.h"

#include <nlohmann/json_fwd.hpp>

static std::string optional_string(const GstStructure *structure, const char *field) {
    const gchar *value = gst_structure_get_string(structure, field);
    return value ? value : "";
}

template <typename Entry> static nlohmann::json output_json(const std::vector<Entry> &outputs) {
    auto result = nlohmann::json::array();
    for (const auto &output : outputs) {
        nlohmann::json entry{{"payloadType", output.payloadType},
                             {"producerId", output.producerId}};
        if constexpr (requires { output.consumerId; }) {
            entry["consumerId"] = output.consumerId;
            entry["inputId"] = output.inputId;
        }
        result.push_back(std::move(entry));
    }
    return result;
}

std::optional<ModelStatus> model_status_from_registration(const GstStructure *structure) {
    const gchar *model_name = gst_structure_get_string(structure, "model-name");
    const gchar *element_name = gst_structure_get_string(structure, "element-name");
    if (model_name == nullptr || element_name == nullptr)
        return std::nullopt;

    gboolean active = FALSE;
    gst_structure_get_boolean(structure, "active", &active);
    return ModelStatus{
        .name = model_name,
        .active = active != FALSE,
        .element_name = element_name,
        .display_name = optional_string(structure, "display-name"),
        .task = optional_string(structure, "task"),
        .runtime = optional_string(structure, "runtime"),
        .provided_outputs =
            opk::result_requirement_event::readOutputs(structure, "provided-outputs"),
        .required_inputs = opk::result_requirement_event::readInputs(structure, "required-inputs"),
        .available_inputs =
            opk::result_requirement_event::readOutputs(structure, "available-inputs"),
    };
}

void ModelRegistry::add_model(const ModelStatus &status) {

    {
        std::lock_guard<std::mutex> lock(model_registry_mutex);
        model_registry[status.element_name] = status;
    }

    opk::log::info("[opksink] Registered model: {} from element: {} (active: {})\n",
                   status.name,
                   status.element_name,
                   status.active ? "yes" : "no");

    trigger_reporting();
}

void ModelRegistry::del_model(const std::string &element_name) {

    {
        std::lock_guard<std::mutex> lock(model_registry_mutex);
        auto it = model_registry.find(element_name);
        if (it != model_registry.end()) {
            opk::log::info("[opksink] Unregistered model: {} from element: {}\n",
                           it->second.name,
                           element_name);
            model_registry.erase(it);
        }
    }

    trigger_reporting();
}

nlohmann::json ModelRegistry::report() const {
    using namespace nlohmann;

    json ret = json::array();

    std::lock_guard<std::mutex> lock(model_registry_mutex);

    for (auto &[name, status] : model_registry) {
        json model = json::object({
            {"name", status.name},
            {"active", status.active},
            {"element_name", status.element_name},
            {"providedOutputs", output_json(status.provided_outputs)},
            {"requiredInputs", output_json(status.required_inputs)},
            {"availableInputs", output_json(status.available_inputs)},
        });
        if (!status.display_name.empty())
            model["displayName"] = status.display_name;
        if (!status.task.empty())
            model["task"] = status.task;
        if (!status.runtime.empty())
            model["runtime"] = status.runtime;
        ret.push_back(std::move(model));
    }

    return ret;
}
