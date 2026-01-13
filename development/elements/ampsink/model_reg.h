#ifndef __MODEL_REGISTRY_H__
#define __MODEL_REGISTRY_H__

#include "nlohmann/json_fwd.hpp"
#include <map>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

struct ModelStatus {
    std::string name;
    bool active;
    std::string element_name;
};

class ModelRegistry {
    std::mutex model_registry_mutex;
    std::map<std::string, ModelStatus> model_registry; // key: element_name

  public:
    void add_model(const std::string &model_name, const std::string &element_name, bool active);
    void del_model(const std::string &element_name);

    nlohmann::json enumerate_models();
    void model_toggle(const std::string &element_name, bool active);
};

#endif // !__MODEL_REGISTRY_H__
