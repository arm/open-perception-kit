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

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---

bool Tools::getTensorShape(const Ort::Session& session, uflw::TensorInOut tensorInOut, int inOutIndex, uflw::Shape& outShape) {

    Ort::TypeInfo ti = (tensorInOut == uflw::TensorInOut::In) ? session.GetInputTypeInfo(inOutIndex) : session.GetOutputTypeInfo(inOutIndex);

    auto tensor = ti.GetTensorTypeAndShapeInfo();

    if(tensor.GetShape().size() > 8) return false;

    outShape.dimensionCount = tensor.GetShape().size();
    auto onnxShape = tensor.GetShape();
    for(size_t i = 0; i < outShape.dimensionCount; i++) {
        outShape.valueCount[i] = onnxShape[i];
        if(~outShape.valueCount[i] == 0) outShape.valueCount[i] = 0;
    }

    return true;

}

bool Tools::onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, uflw::ValueType& outUniflowType) {
    if(onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        outUniflowType = uflw::ValueType::f32;
        return true;
    }
    if(onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        outUniflowType = uflw::ValueType::f16;
        return true;
    }
    if(onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
        outUniflowType = uflw::ValueType::i8;
        return true;
    }
    if(onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8) {
        outUniflowType = uflw::ValueType::u8;
        return true;
    }
    if(onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        outUniflowType = uflw::ValueType::i64;
        return true;
    }
    return false;
}

uflw::ModelFamily Tools::guessModelFamily(const Ort::Session& session, uflw::FxString<32>& outVersion) {

    Ort::AllocatorWithDefaultOptions alloc;

    // Try model metadata
    Ort::ModelMetadata meta = session.GetModelMetadata();
    auto keys = meta.GetCustomMetadataMapKeysAllocated(alloc);
    for (auto& k : keys) {
        std::string key = toLower(k.get());
        auto valAlloc = meta.LookupCustomMetadataMapAllocated(k.get(), alloc);
        std::string val = valAlloc ? toLower(std::string(valAlloc.get())) : "";

        printf("[%s]:[%s]\n", key.c_str(), val.c_str());

        // version
        if (key.find("version") != std::string::npos || key.find("model_version") != std::string::npos)
            outVersion = val;

        // YOLO (Ultralytics / Darknet)
        if (key.find("author") != std::string::npos && val.find("ultralytics") != std::string::npos)
            return uflw::ModelFamily::YoloObjectDetection;
        if (key.find("task") != std::string::npos && val.find("detect") != std::string::npos)
            return uflw::ModelFamily::YoloObjectDetection;
        if (val.find("yolo") != std::string::npos)
            return uflw::ModelFamily::YoloObjectDetection;

        // BlazeFace
        if (val.find("blazeface") != std::string::npos)
            return uflw::ModelFamily::BlazeFace;

        // EfficientDet
        if (val.find("efficientdet") != std::string::npos)
            return uflw::ModelFamily::EfficientDet;

        // CLIP
        if (val.find("clip") != std::string::npos)
            return uflw::ModelFamily::Clip;

        // Whisper (speech-to-text)
        if (val.find("whisper") != std::string::npos)
            return uflw::ModelFamily::Whisper;
    }

    // --- If metadata missing, look at input/output names as fallback ---
    size_t num_inputs = session.GetInputCount();
    size_t num_outputs = session.GetOutputCount();

    // prefer outputs, maybe their names are more verbose
    for (size_t i = 0; i < num_outputs; ++i) {
        auto nameAlloc = session.GetOutputNameAllocated(i, alloc);
        std::string name = toLower(nameAlloc.get());

        if (name.find("selectedboxes") != std::string::npos)
            return uflw::ModelFamily::BlazeFace;
        if (name.find("boxes") != std::string::npos || name.find("yolo") != std::string::npos)
            return uflw::ModelFamily::YoloObjectDetection;
        if (name.find("det") != std::string::npos && name.find("class") != std::string::npos)
            return uflw::ModelFamily::EfficientDet;
        if (name.find("seg") != std::string::npos)
            return uflw::ModelFamily::Segmentation;
    }

    // fallback to inputs
    for (size_t i = 0; i < num_inputs; ++i) {
        auto nameAlloc = session.GetInputNameAllocated(i, alloc);
        std::string name = toLower(nameAlloc.get());
        if (name.find("image") != std::string::npos && num_outputs == 1)
            return uflw::ModelFamily::Classification;
        if (name.find("mel") != std::string::npos || name.find("audio") != std::string::npos)
            return uflw::ModelFamily::Whisper;
        if (name.find("clip") != std::string::npos)
            return uflw::ModelFamily::Clip;
    }

    auto nodeCount = session.GetOverridableInitializerCount();
    if (nodeCount > 0) {
        // use node/operator patterns here
    }

    return uflw::ModelFamily::Unknown;
}

uflw::TensorDataKind Tools::guessModelInputDataKind(const Ort::Session& session, int inputIndex, int& outBatchCount) {

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

    // quick text check
    /*if (valueType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 || valueType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
        if (name.find("token") != std::string::npos || name.find("ids") != std::string::npos)
            return uflw::InputTensorDataKind::TextDUMMY;
    }*/

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

}

std::map<std::string, std::string> Tools::getModelMeta(const Ort::Session& session) {
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

amp::Result<uflw::Model> Tools::inspectModel(const Ort::Session& session) {

    std::map<std::string, std::string> meta = Tools::getModelMeta(session);

    // ---- -------------------------------------------

    uflw::Model model;
    //model.modelFileName = modelFile;

    uflw::FxString<32> modelVersion;
    model.modelFamily = guessModelFamily(session, modelVersion);
    
    Ort::AllocatorWithDefaultOptions allocator;
    model.modelInputCount = session.GetInputCount();
    model.modelOutputCount = session.GetOutputCount();

    for (size_t i = 0; i < model.modelInputCount; ++i) {
        Ort::TypeInfo ti = session.GetInputTypeInfo(i);

        auto tensor = ti.GetTensorTypeAndShapeInfo();
        // name
        model.inputs[i].name = session.GetInputNameAllocated(i, allocator).get();
        // tensor value type
        if(!onnxTypeToUniflowType(tensor.GetElementType(), model.inputs[i].valueType)) model.parseError = "unknown input tensor value type";
        if(uflw::ValueType::f32 != model.inputs[i].valueType && uflw::ValueType::i64 != model.inputs[i].valueType) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, "only float32 or int64 input tensors are supported in ONNX") };
        } 
        // shape
        if(false == getTensorShape(session, uflw::TensorInOut::In, i, model.inputs[i].shape)) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, "too many dimensions in input tensor") };
        }
                
        // input format
        model.inputs[i].dataKind = guessModelInputDataKind(session, i, model.inputs[i].batch);
        if(model.inputs[i].dataKind == uflw::TensorDataKind::Unknown) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, "input tensor data kind dicovery failed") };
        }
        // etc
        model.inputs[i].quantArguments.valueType = model.inputs[i].valueType;
        model.inputs[i].quantArguments.scale = 1.0f;
        model.inputs[i].quantArguments.zeroPoint = 0.0f;
    }

    for (size_t i = 0; i < model.modelOutputCount; ++i) {
        Ort::TypeInfo ti = session.GetOutputTypeInfo(i);

        auto tensor = ti.GetTensorTypeAndShapeInfo();
        // name
        model.outputs[i].name = session.GetOutputNameAllocated(i, allocator).get();
        // tensor value type
        if(!onnxTypeToUniflowType(tensor.GetElementType(), model.outputs[i].valueType)) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, "unknown input tensor value type") };
        }
        if(uflw::ValueType::f32 != model.outputs[i].valueType) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, "only float32 output tensors are supported in ONNX") };
        }        
        // shape
        if(false == getTensorShape(session, uflw::TensorInOut::Out, i, model.outputs[i].shape)) {
            return tl::unexpected{ AMP_ERROR(amp::ErrorFlag::ModelInspectError, "too many dimensions in output tensor") };
        }

        // etc
        model.outputs[i].quantArguments.valueType = model.outputs[i].valueType;
        model.outputs[i].quantArguments.scale = 1.0f;
        model.outputs[i].quantArguments.zeroPoint = 0.0f;

    }

    return model;

}

std::string Tools::toString(const uflw::Model& model) {
    std::string ret;
    
    ret += fmt::format("Model: [{}]\n", uflw::toString(model.modelFamily).c_str());
    ret += fmt::format("Version: [{}]\n", model.version.c_str());
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


