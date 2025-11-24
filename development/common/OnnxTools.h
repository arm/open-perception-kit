#pragma once

#include <onnxruntime_cxx_api.h>

#include "uniflow/fixed_string.h"
#include "uniflow/public_types.h"
#include "uniflow/uniflow.h"
#include "uniflow/yolo_like_parser.h"
#include "uniflow/model_io.h"

#include <vector>

struct IoInfo {
    std::string name;
    std::vector<int64_t> shape; // -1 for dynamic
    std::string onnx_type;      // e.g., "tensor(float)"
};

struct ModelIntrospection {
    std::unordered_map<std::string, std::string> metadata;
    std::vector<IoInfo> inputs;
    std::vector<IoInfo> outputs;
};

struct OnnxTools {

    static bool onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, uflw::ValueType& outUniflowType);
    static bool getTensorShape(const Ort::Session& session, uflw::TensorInOut tensorInOut, int inputIndex, uflw::Shape& outShape);

    static uflw::ModelFamily guessModelFamily(const Ort::Session& session, uflw::FxString<32>& outVersion);
    static uflw::InputTensorDataKind guessModelInputDataKind(const Ort::Session& session, int inputIndex, int& outBatchCount);

    static uflw::Model inspectModel(const Ort::Session& session);

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
