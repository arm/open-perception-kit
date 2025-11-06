#pragma once

#include <onnxruntime_cxx_api.h>

#include "uniflow/public_types.h"
#include "uniflow/uniflow.h"

struct OnnxTools {

    static size_t getOnnxValueTypeByteSize(ONNXTensorElementDataType tensorType);

};

struct OnnxOutputTensor {

    OnnxOutputTensor(const std::vector<Ort::Value>& runResult);

    uflw::Shape getShape() const;
    size_t getValueCount() const;

    size_t getValueByteSize() const;
    size_t getByteSize() const;
    const void* getRawData() const;

    const std::vector<Ort::Value>& runResult;

    bool dump(const std::string& fileName) const;

};
