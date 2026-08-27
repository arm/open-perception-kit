/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "model_reg.h"

#include "Log.h"

#include <nlohmann/json_fwd.hpp>

void ModelRegistry::add_model(const std::string &model_name,
                              const std::string &element_name,
                              bool active,
                              std::string_view display_name,
                              std::string_view task,
                              std::string_view runtime,
                              const std::vector<std::string> &provided_content_types,
                              const std::vector<std::string> &required_content_types) {

    {
        ModelStatus status;
        status.name = model_name;
        status.active = active;
        status.element_name = element_name;
        status.display_name = display_name;
        status.task = task;
        status.runtime = runtime;
        status.provided_content_types = provided_content_types;
        status.required_content_types = required_content_types;
        std::lock_guard<std::mutex> lock(model_registry_mutex);
        model_registry[element_name] = status;
    }

    pek::log::info("[peksink] Registered model: {} from element: {} (active: {})\n",
                   model_name,
                   element_name,
                   active ? "yes" : "no");

    trigger_reporting();
}

void ModelRegistry::del_model(const std::string &element_name) {

    {
        std::lock_guard<std::mutex> lock(model_registry_mutex);
        auto it = model_registry.find(element_name);
        if (it != model_registry.end()) {
            pek::log::info("[peksink] Unregistered model: {} from element: {}\n",
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
            {"providedContentTypes", status.provided_content_types},
            {"requiredContentTypes", status.required_content_types},
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
