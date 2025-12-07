#include <cstdint>
#include <onnxruntime_cxx_api.h>

#include "uniflow/detection_types.h"
#include "uniflow/fixed_string.h"
#include "uniflow/public_types.h"
#include "uniflow/uniflow.h"
#include "uniflow/yolo_like_parser.h"
#include "uniflow/model_io.h"

#include "JsonSchemas.h" 

#include <vector>
#include <map>

namespace onnx {

struct Tensor {

    Tensor(const uflw::Shape& shape, uflw::ValueType type) {
        this->shape = shape;
        this->type = type;
        this->typeByteSize = uflw::getValueTypeByteSize(type);
        for(size_t i = 0; i < shape.dimensionCount; i++)
            onnxShape[i] = shape.valueCount[i];

        this->data.resize(shape.getFullValueCount() * typeByteSize);
    }

    uint8_t* getData() { 
        return (uint8_t*)data.data(); 
    }

    size_t getElementCount() { 
        size_t elemCount = data.size() / typeByteSize;
        return elemCount; 
    }
    
    size_t getByteCount() { return data.size(); }

    bool checkShape(const uflw::Shape& shape) { return this->shape == shape; }

    Ort::Value createOnnxTensor(const Ort::MemoryInfo& memInfo) {
        if (this->type == uflw::ValueType::f32) {
            return Ort::Value::CreateTensor<float>(
            memInfo,
            reinterpret_cast<float*>(getData()),
            getElementCount(),
            this->onnxShape,
            this->shape.dimensionCount);
        } else if (this->type == uflw::ValueType::i64) {
            return Ort::Value::CreateTensor<int64_t>(
                memInfo,
                reinterpret_cast<int64_t*>(getData()),
                getElementCount(),
                onnxShape,
                this->shape.dimensionCount);
        } else {
            assert(0);
        }
    }
    
private:

    uflw::ValueType type;
    size_t typeByteSize;
    uflw::Shape shape;
    int64_t onnxShape[8];

    std::vector<uint8_t> data;

};
}


