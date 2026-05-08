/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "model_reg.h"

#include <nlohmann/json_fwd.hpp>

#include <iostream>

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

    std::cout << "[peksink] Registered model: " << model_name << " from element: " << element_name
              << " (active: " << (active ? "yes" : "no") << ")" << std::endl;

    trigger_reporting();
}

void ModelRegistry::del_model(const std::string &element_name) {

    {
        std::lock_guard<std::mutex> lock(model_registry_mutex);
        auto it = model_registry.find(element_name);
        if (it != model_registry.end()) {
            std::cout << "[peksink] Unregistered model: " << it->second.name
                      << " from element: " << element_name << std::endl;
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
