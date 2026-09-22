/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "model_reg.h"

#include "Log.h"

#include <nlohmann/json_fwd.hpp>

static std::string optional_string(const GstStructure *structure, const char *field) {
    const gchar *value = gst_structure_get_string(structure, field);
    return value ? value : "";
}

static std::vector<std::string> content_types(const GstStructure *structure, const char *field) {
    std::vector<std::string> result;
    const GValue *values = gst_structure_get_value(structure, field);
    if (values == nullptr)
        return result;
    if (!G_VALUE_HOLDS(values, G_TYPE_STRV))
        return result;

    if (const auto types = static_cast<const gchar *const *>(g_value_get_boxed(values));
        types != nullptr)
        for (const gchar *const *type = types; *type != nullptr; ++type)
            result.emplace_back(*type);
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
        .provided_content_types = content_types(structure, "provided-content-types"),
        .required_content_types = content_types(structure, "required-content-types"),
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
