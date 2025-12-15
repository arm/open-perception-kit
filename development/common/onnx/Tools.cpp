#include "Tools.h"

#include "onnxruntime_c_api.h"
#include "onnxruntime_cxx_api.h"
#include "tl/expected.hpp"
#include "uniflow/fixed_string.h"
#include "uniflow/model_io.h"
#include "uniflow/public_types.h"

#include <fmt/core.h>

#include "amp/Result.h"

using namespace onnx;

bool Tools::onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, uflw::ValueType& outType) {
    if(onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) { outType = uflw::ValueType::f32; return true; }
    if(onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) { outType = uflw::ValueType::f16; return true; }
    if(onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) { outType = uflw::ValueType::i8; return true; }
    if(onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8) { outType = uflw::ValueType::u8; return true; }
    if(onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) { outType = uflw::ValueType::i64; return true; }
    return false;
}

std::map<std::string, std::string> Tools::getModelMeta(const Ort::Session& session) {
    
    auto toLower = [](std::string s) -> std::string {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return s;
    };

    // ---

    std::map<std::string, std::string> ret;

    Ort::AllocatorWithDefaultOptions alloc;
    Ort::ModelMetadata meta = session.GetModelMetadata();

    std::vector<Ort::AllocatedStringPtr> keys = meta.GetCustomMetadataMapKeysAllocated(alloc);
    for (const auto& k : keys) {
        Ort::AllocatedStringPtr v = meta.LookupCustomMetadataMapAllocated(k.get(), alloc);
        if(v) ret[toLower(k.get())] = toLower(v.get());
    }

    return ret;
}

// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---

std::vector<size_t> Tools::getTensorShape(const Ort::Session& session, uflw::TensorInOut tensorInOut, int tensorIndex) {
    Ort::TypeInfo ti = (tensorInOut == uflw::TensorInOut::In) ? session.GetInputTypeInfo(tensorIndex) : session.GetOutputTypeInfo(tensorIndex);
    auto tensor = ti.GetTensorTypeAndShapeInfo();
    std::vector<size_t> dims;
    for(const auto& a : tensor.GetShape())
        dims.push_back(a);
    return dims;
}

/*uflw::TensorDataKind Tools::guessModelInputDataKind(const Ort::Session& session, int inputIndex, int& outBatchCount) {

    outBatchCount = 0;

    Ort::AllocatorWithDefaultOptions alloc;
    int inputCount = (int)session.GetInputCount();
    if (inputIndex < 0 || inputCount == 0 || inputIndex >= inputCount) return uflw::TensorDataKind::Unknown;

    uflw::Shape shape;
    if(false == getTensorShape(session, uflw::TensorInOut::In, inputIndex,shape))
        return uflw::TensorDataKind::Unknown;

    Ort::TypeInfo typeInfo = session.GetInputTypeInfo(inputIndex);
    ONNXType onnxType = typeInfo.GetONNXType();
    if (onnxType != ONNX_TYPE_TENSOR) return uflw::TensorDataKind::Unknown;
    ONNXTensorElementDataType valueType = typeInfo.GetTensorTypeAndShapeInfo().GetElementType();
    
    std::string name = session.GetInputNameAllocated(inputIndex, alloc).get();

    // float data
    if (valueType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || valueType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        if (shape.dimensionCount == 1) {
                return uflw::TensorDataKind::Value;
        }
        else if (shape.dimensionCount == 3) {
            if (name.find("mel") != std::string::npos || name.find("audio") != std::string::npos)
                return uflw::TensorDataKind::AudioDUMMY;
            if (shape.valueCount[1] < 128 && shape.valueCount[2] > 128) // typical mel [N, F, T]
                return uflw::TensorDataKind::AudioDUMMY;
        }
        else if (shape.dimensionCount == 4) {
            if(shape.valueCount[1] == 3) {
                outBatchCount = shape.valueCount[0];
                return uflw::TensorDataKind::ImageRgbChw;
            }
            if(shape.valueCount[3] == 3) {
                outBatchCount = shape.valueCount[0];
                return uflw::TensorDataKind::ImageRgbHwc;
            }
            if(shape.valueCount[1] == 1) {
                outBatchCount = shape.valueCount[0];
                return uflw::TensorDataKind::ImageGray;
            }
            if(shape.valueCount[3] == 1) {
                outBatchCount = shape.valueCount[0];
                return uflw::TensorDataKind::ImageGray;
            }
        }
    }
    
    return uflw::TensorDataKind::Unknown;

}*/

amp::Result<uflw::Model> Tools::inspectModel(const Ort::Session& session) {

    uflw::Model model;
    
    Ort::AllocatorWithDefaultOptions allocator;
    model.modelInputCount = session.GetInputCount();
    model.modelOutputCount = session.GetOutputCount();

    // inspect all the INPUT TENSORS
    for (size_t i = 0; i < model.modelInputCount; ++i) {
        Ort::TypeInfo ti = session.GetInputTypeInfo(i);

        auto tensor = ti.GetTensorTypeAndShapeInfo();
        // name
        model.inputs[i].name = session.GetInputNameAllocated(i, allocator).get();
        
        // tensor value type
        uflw::ValueType tensorValueType;
        if(false == onnxTypeToUniflowType(tensor.GetElementType(), tensorValueType)) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, fmt::format("cannot recognize input ONNX type: {}", (uint64_t)tensor.GetElementType())) };
        }

        if(uflw::ValueType::f32 != tensorValueType && uflw::ValueType::i64 != tensorValueType) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, "only float32 or int64 input tensors are supported in ONNX") };
        }    
        model.inputs[i].valueType = tensorValueType;

        // shape
        std::vector<size_t> onnxDims = getTensorShape(session, uflw::TensorInOut::In, i);
        if(onnxDims.size() < 1 || onnxDims.size() > 8) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, "input tensor size must be between 1 and 8") };
        }
        model.inputs[i].shape.setFrom(onnxDims);                

        fmt::print("Input shape {}\n", model.inputs[i].shape.toString().c_str()); 

    }

    // inspect all the OUTPUT TENSORS
    for (size_t i = 0; i < model.modelOutputCount; ++i) {
        Ort::TypeInfo ti = session.GetOutputTypeInfo(i);

        auto tensor = ti.GetTensorTypeAndShapeInfo();
        // name
        model.outputs[i].name = session.GetOutputNameAllocated(i, allocator).get();
        
        // tensor value type
        uflw::ValueType tensorValueType;
        if(false == onnxTypeToUniflowType(tensor.GetElementType(), tensorValueType)) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, fmt::format("cannot recognize output ONNX type: {}", (uint64_t)tensor.GetElementType())) };
        }

        if(uflw::ValueType::f32 != tensorValueType && uflw::ValueType::i64 != tensorValueType) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, "only float32 or int64 input tensors are supported in ONNX") };
        }    
        model.outputs[i].valueType = tensorValueType;

        // shape
        std::vector<size_t> onnxDims = getTensorShape(session, uflw::TensorInOut::Out, i);
        if(onnxDims.size() < 1 || onnxDims.size() > 8) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, "output tensor size must be between 1 and 8") };
        }
        model.outputs[i].shape.setFrom(onnxDims);                
        fmt::print("Output shape {}\n", model.outputs[i].shape.toString().c_str()); 
}

    return model;
}



std::string Tools::toString(const uflw::Model& model) {
    std::string ret;
    
    ret += fmt::format("Model: [{}]\n", model.modelFamily.c_str());
    ret += fmt::format("Input count: {}\n", model.modelInputCount);
    ret += fmt::format("Output count: {}\n", model.modelOutputCount);

    for(size_t i = 0; i < model.modelInputCount; i++) {
        ret += fmt::format("Input #{} [{}]\n", i, model.inputs[i].name.c_str());
        ret += fmt::format(" Batch {}\n", model.inputs[i].batch);
        ret += fmt::format(" ValueType: {}\n", uflw::toString(model.inputs[i].valueType).c_str());
        ret += fmt::format(" Shape: {}\n", uflw::toString(model.inputs[i].shape).c_str());        
        ret += fmt::format(" DataKind: {}\n", uflw::toString(model.inputs[i].dataKind).c_str());        
    }
    for(size_t i = 0; i < model.modelOutputCount; i++) {
        ret += fmt::format("Output #{} [{}]\n", i, model.outputs[i].name.c_str());
        ret += fmt::format(" ValueType: {}\n", uflw::toString(model.outputs[i].valueType).c_str());
        ret += fmt::format(" Shape: {}\n", uflw::toString(model.outputs[i].shape).c_str());        
    }

    return ret;
}


