#include "ModelDescriptor.h"
#include "fmt/color.h"
#include "tl/expected.hpp"

#include "amp/Result.h"
#include "amp/File.h"

amp::Result<ModelDescriptor> ModelDescriptor::fromJson(const std::string& jsonString) {

    try {
        json json = json::parse(jsonString);
        return json.get<ModelDescriptor>();
    } catch (const json::exception& e) {
        return tl::unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData, 
            fmt::format("Error occured while parsing ModelDescriptor json: {}", e.what())));
    }
}

amp::Result<ModelDescriptor> ModelDescriptor::fromFile(const std::string& path) {
    auto textResult = amp::fs::loadText(path, true);
    if (!textResult) {
        return tl::unexpected(textResult.error()); 
    }
    return fromJson(*textResult);
}



