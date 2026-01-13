#include "model_reg.h"

#include <iostream>

void ModelRegistry::add_model(const std::string &model_name,
                              const std::string &element_name,
                              bool active) {

    std::lock_guard<std::mutex> lock(model_registry_mutex);
    ModelStatus status;
    status.name = model_name;
    status.active = active;
    status.element_name = element_name;
    model_registry[element_name] = status;

    std::cout << "[ampsink] Registered model: " << model_name << " from element: " << element_name
              << " (active: " << (active ? "yes" : "no") << ")" << std::endl;
}

void ModelRegistry::del_model(const std::string &element_name) {

    std::lock_guard<std::mutex> lock(model_registry_mutex);
    auto it = model_registry.find(element_name);
    if (it != model_registry.end()) {
        std::cout << "[ampsink] Unregistered model: " << it->second.name
                  << " from element: " << element_name << std::endl;
        model_registry.erase(it);
    }
}

nlohmann::json ModelRegistry::enumerate_models() {
    using namespace nlohmann;

    json models_array = json::array();

    {
        std::lock_guard<std::mutex> lock(model_registry_mutex);
        for (const auto &entry : model_registry) {
            json model_obj = {{"element_name", entry.second.element_name},
                              {"model_name", entry.second.name},
                              {"active", entry.second.active}};
            models_array.push_back(model_obj);
        }
    }

    json response = {{"models", models_array}, {"count", models_array.size()}};

    return response;
}

void ModelRegistry::model_toggle(const std::string &element_name, bool active) {
    // Update the registry
    std::lock_guard<std::mutex> lock(model_registry_mutex);
    for (auto &entry : model_registry) {
        if (entry.second.element_name == element_name) {
            entry.second.active = active;
            break;
        }
    }
}
