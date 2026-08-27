/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#ifndef __MODEL_REGISTRY_H__
#define __MODEL_REGISTRY_H__

#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "status_reporter.h"

struct ModelStatus {
    std::string name;
    bool active;
    std::string element_name;
    std::string display_name;
    std::string task;
    std::string runtime;
    std::vector<std::string> provided_content_types;
    std::vector<std::string> required_content_types;
};

class ModelRegistry : public StatusReporter {
    mutable std::mutex model_registry_mutex;
    std::map<std::string, ModelStatus> model_registry; // key: element_name

  public:
    void add_model(const std::string &model_name,
                   const std::string &element_name,
                   bool active,
                   std::string_view display_name = "",
                   std::string_view task = "",
                   std::string_view runtime = "",
                   const std::vector<std::string> &provided_content_types = {},
                   const std::vector<std::string> &required_content_types = {});
    void del_model(const std::string &element_name);

    nlohmann::json report() const override;
};

#endif // !__MODEL_REGISTRY_H__
