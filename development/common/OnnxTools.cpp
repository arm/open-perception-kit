#include "OnnxTools.h"
#include "onnxruntime_c_api.h"
#include "onnxruntime_cxx_api.h"
#include "uniflow/fixed_string.h"
#include "uniflow/model_io.h"
#include "uniflow/public_types.h"

#define FMT_HEADER_ONLY
#include <fmt/core.h>

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

// ---

bool safeParseInt(const char *str, int *out) {
    char *endptr;
    errno = 0; 

    long val = strtol(str, &endptr, 10);  // base 10

    if (endptr == str) return false;
    if (*endptr != '\0') return false;
    if ((errno == ERANGE) || (val > INT_MAX) || (val < INT_MIN)) return false; 

    *out = (int)val;
    return true;
}

const char* getOnnxValueTypeName(ONNXTensorElementDataType t) {
    switch (t) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED: return "UNDEFINED";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:     return "FLOAT";     // 1
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:     return "UINT8";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:      return "INT8";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:    return "UINT16";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:     return "INT16";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:     return "INT32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:     return "INT64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING:    return "STRING";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:      return "BOOL";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:   return "FLOAT16";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:    return "DOUBLE";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:    return "UINT32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:    return "UINT64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64: return "COMPLEX64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128:return "COMPLEX128";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16:  return "BFLOAT16";
        default: return "<unknown>";
    }
}

size_t getOnnxValueTypeByteSize(ONNXTensorElementDataType tensorType) {
    switch (tensorType)
    {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:        return 4;  // float32
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:        return 1;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:         return 1;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:       return 2;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:        return 2;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:        return 4;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:        return 8;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING:       return sizeof(char*); // variable length
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:         return 1;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:      return 2;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:       return 8;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:       return 4;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:       return 8;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64:    return 8;  // 2 * float32
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128:   return 16; // 2 * float64
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16:     return 2;
        default: return 0;
    }
}

// --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- ---

bool OnnxTools::getTensorShape(const Ort::Session& session, uflw::TensorInOut tensorInOut, int inputIndex, uflw::Shape& outShape) {

    Ort::AllocatorWithDefaultOptions allocator;
    
    Ort::TypeInfo ti = (tensorInOut == uflw::TensorInOut::In) ? session.GetInputTypeInfo(inputIndex) : session.GetOutputTypeInfo(inputIndex);

    auto tensor = ti.GetTensorTypeAndShapeInfo();

    if(tensor.GetShape().size() > 8) return false;

    outShape.dimensionCount = tensor.GetShape().size();
    for(size_t i = 0; i < tensor.GetShape().size(); i++) outShape.valueCount[i] = tensor.GetShape()[i];

    return true;

}

bool OnnxTools::onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, uflw::ValueType& outUniflowType) {
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

uflw::ModelFamily OnnxTools::guessModelFamily(const Ort::Session& session, uflw::FxString<32>& outVersion) {

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

uflw::InputTensorDataKind OnnxTools::guessModelInputDataKind(const Ort::Session& session, int inputIndex, int& outBatchCount) {

    outBatchCount = 0;

    Ort::AllocatorWithDefaultOptions alloc;
    int inputCount = (int)session.GetInputCount();
    if (inputIndex < 0 || inputCount == 0 || inputIndex >= inputCount) return uflw::InputTensorDataKind::Unknown;

    uflw::Shape shape;
    if(false == getTensorShape(session, uflw::TensorInOut::In, inputIndex,shape))
        return uflw::InputTensorDataKind::Unknown;

    Ort::TypeInfo typeInfo = session.GetInputTypeInfo(inputIndex);
    ONNXType onnxType = typeInfo.GetONNXType();
    if (onnxType != ONNX_TYPE_TENSOR) return uflw::InputTensorDataKind::Unknown;
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
                return uflw::InputTensorDataKind::Value;
        }
        else if (shape.dimensionCount == 3) {
            if (name.find("mel") != std::string::npos || name.find("audio") != std::string::npos)
                return uflw::InputTensorDataKind::AudioDUMMY;
            if (shape.valueCount[1] < 128 && shape.valueCount[2] > 128) // typical mel [N, F, T]
                return uflw::InputTensorDataKind::AudioDUMMY;
        }
        else if (shape.dimensionCount == 4) {
            if(shape.valueCount[1] == 3) {
                outBatchCount = shape.valueCount[0];
                return uflw::InputTensorDataKind::ImageRgbChw;
            }
            if(shape.valueCount[3] == 3) {
                outBatchCount = shape.valueCount[0];
                return uflw::InputTensorDataKind::ImageRgbHwc;
            }
            if(shape.valueCount[1] == 1) {
                outBatchCount = shape.valueCount[0];
                return uflw::InputTensorDataKind::ImageGray;
            }
            if(shape.valueCount[3] == 1) {
                outBatchCount = shape.valueCount[0];
                return uflw::InputTensorDataKind::ImageGray;
            }
        }
    }
    
    return uflw::InputTensorDataKind::Unknown;

}

std::map<std::string, std::string> OnnxTools::getModelMeta(const Ort::Session& session) {
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

uflw::Model OnnxTools::inspectModel(const Ort::Session& session, const std::string& modelFile) {

    std::map<std::string, std::string> meta = OnnxTools::getModelMeta(session);

    // ---- -------------------------------------------

    uflw::Model model;
    model.modelFileName = modelFile;

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
        printf("%s\n", model.inputs[i].name.c_str());
        // tensor value type
        if(!onnxTypeToUniflowType(tensor.GetElementType(), model.inputs[i].valueType)) model.parseError = "unknown input tensor value type";
        if(uflw::ValueType::f32 != model.inputs[i].valueType && uflw::ValueType::i64 != model.inputs[i].valueType) {
            model.parseError = "only float32 or int64 input tensors are supported in ONNX";
            return model;
        } 
        // shape
        if(false == getTensorShape(session, uflw::TensorInOut::In, i, model.inputs[i].shape)) {
            model.parseError = "too many dimensions in input tensor";
            return model;
        }
        printf("%s\n", uflw::toString(model.inputs[i].valueType).c_str());
        printf("%s\n", model.inputs[i].shape.toString().c_str());
        // input format
        model.inputs[i].dataKind = guessModelInputDataKind(session, i, model.inputs[i].batch);
        if(model.inputs[i].dataKind == uflw::InputTensorDataKind::Unknown) {
            model.parseError = "input tensor data kind dicovery failed";      
            return model;
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
            model.parseError = "unknown input tensor value type";
            return model;
        }
        if(uflw::ValueType::f32 != model.outputs[i].valueType) {
            model.parseError = "only float32 output tensors are supported in ONNX";
            return model;
        }        
        // shape
        if(false == getTensorShape(session, uflw::TensorInOut::Out, i, model.outputs[i].shape)) {
            model.parseError = "too many dimensions in output tensor";
            return model;
        }
        // etc
        model.outputs[i].quantArguments.valueType = model.outputs[i].valueType;
        model.outputs[i].quantArguments.scale = 1.0f;
        model.outputs[i].quantArguments.zeroPoint = 0.0f;

    }

    return model;

}

std::string OnnxTools::toString(const uflw::Model& model) {
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

// ---
/*
OnnxOutputTensor::OnnxOutputTensor(const std::vector<Ort::Value>& runResult) : runResult(runResult) {

}

size_t OnnxOutputTensor::getValueByteSize() const {
    if(this->runResult.size() == 0) return 0;
    return getOnnxValueTypeByteSize(this->runResult.at(0).GetTensorTypeAndShapeInfo().GetElementType());
}

uflw::Shape OnnxOutputTensor::getShape() const {
    uflw::Shape shape;

    if(this->runResult.size() == 0) return shape;

    std::vector<int64_t> originalShape = this->runResult.at(0).GetTensorTypeAndShapeInfo().GetShape();

    for(size_t i = 0; i < sizeof(shape.valueCount); i++) {
        if(i < originalShape.size()) {
            shape.valueCount[i] = originalShape[i];
        }
    }
    shape.dimensionCount = originalShape.size();

    return shape;
}

size_t OnnxOutputTensor::getValueCount() const {
 
    if(this->runResult.size() == 0) return 0;

    uflw::Shape shape = this->getShape();
    
    size_t valueCount = 1;
    for(size_t i = 0; i < shape.dimensionCount; i++) {
        valueCount *= shape.valueCount[i];
    }

    return valueCount;
}

size_t OnnxOutputTensor::getByteSize() const {
    if(this->runResult.size() == 0) return 0;

    return this->getValueByteSize() * this->getValueCount();
}

const void* OnnxOutputTensor::getRawData() const {
    if(this->runResult.size() == 0) return nullptr;
    return this->runResult.at(0).GetTensorRawData();
}

bool OnnxOutputTensor::dump(const std::string& fileName) const {

    const void* raw = getRawData();
    size_t amount = getByteSize();

    if(!raw || !amount) return false;

    FILE* f = fopen(fileName.c_str(), "wb");

    if(!f) return false;

    if(amount < fwrite(raw, 1, amount, f)) {
        fclose(f);
        return false;
    }
    
    fclose(f);
    return true;
}
*/
/*uflw::YoloLikeParser::ModelOutput OnnxTools::getOutputFromYoloModel(const Ort::Session* session, int index) {

    uflw::YoloLikeParser::ModelOutput out;

    Ort::AllocatorWithDefaultOptions alloc;
    Ort::ModelMetadata meta = session->GetModelMetadata();

    // ---

    std::vector<Ort::AllocatedStringPtr> keys = meta.GetCustomMetadataMapKeysAllocated(alloc);

    for (const auto& k : keys) {
        
        Ort::AllocatedStringPtr v = meta.LookupCustomMetadataMapAllocated(k.get(), alloc);
        if(v) {
            if(!strcmp(k.get(), "stride")) {
                int stride;
                if(safeParseInt(v.get(), &stride)) {
                    out.detectionStepStride = stride;
                }
            }
        }


        //printf("Meta[%s] = %s\n", k.get(), v ? v.get() : "(null)");
    
    }  

    // ---

    return out;

}*/