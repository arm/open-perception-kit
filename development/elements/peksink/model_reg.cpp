/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "model_reg.h"

#include "Log.h"

#include <nlohmann/json_fwd.hpp>

void ModelRegistry::add_model(const std::string &model_name,
                              const std::string &element_name,
                              bool active) {

    {
        ModelStatus status;
        status.name = model_name;
        status.active = active;
        status.element_name = element_name;
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

void ModelRegistry::toggle_model(const std::string &element_name, bool active) {
    if (auto it = model_registry.find(element_name); it != model_registry.end()) {
        (*it).second.active = active;
        trigger_reporting();
    }
}

nlohmann::json ModelRegistry::report() const {
    using namespace nlohmann;

    json ret = json::array();

    std::lock_guard<std::mutex> lock(model_registry_mutex);

    for (auto &[name, status] : model_registry) {
        ret.push_back(json::object({
            {"name", status.name},
            {"active", status.active},
            {"element_name", status.element_name},
        }));
    }

    return ret;
}
