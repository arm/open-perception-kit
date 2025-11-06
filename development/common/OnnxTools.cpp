#include "OnnxTools.h"

size_t OnnxTools::getOnnxValueTypeByteSize(ONNXTensorElementDataType tensorType) {
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

// ---

OnnxOutputTensor::OnnxOutputTensor(const std::vector<Ort::Value>& runResult) : runResult(runResult) {

}

size_t OnnxOutputTensor::getValueByteSize() const {
    if(this->runResult.size() == 0) return 0;
    return OnnxTools::getOnnxValueTypeByteSize(this->runResult.at(0).GetTensorTypeAndShapeInfo().GetElementType());
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

