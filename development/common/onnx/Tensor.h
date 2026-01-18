#pragma once

#include <cstdint>
#include <onnxruntime_cxx_api.h>

#include "amp/Shape.h"
#include "amp/Types.h"

#include <vector>

namespace onnx {

// ONNX level tensor
struct Tensor {

    Tensor(const amp::Shape &shape, amp::ValueType type) {
        this->shape = shape;
        this->type = type;
        this->typeByteSize = amp::getValueTypeByteSize(type);
        for (size_t i = 0; i < shape.dimensionCount; i++)
            onnxShape[i] = shape.valueCount[i];

        if (shape.hasDynamicDimension()) {
            // do nothing
        } else {
            this->data.resize(shape.getFullValueCount() * typeByteSize);
        }
    }

    uint8_t *getData() {
        return (uint8_t *)data.data();
    }

    size_t getElementCount() {
        size_t elemCount = data.size() / typeByteSize;
        return elemCount;
    }

    size_t getByteCount() {
        return data.size();
    }

    bool checkShape(const amp::Shape &shape) {
        return this->shape == shape;
    }

    Ort::Value createOnnxTensor(const Ort::MemoryInfo &memInfo) {
        if (this->type == amp::ValueType::f32) {
            return Ort::Value::CreateTensor<float>(memInfo,
                                                   reinterpret_cast<float *>(getData()),
                                                   getElementCount(),
                                                   this->onnxShape,
                                                   this->shape.dimensionCount);
        } else if (this->type == amp::ValueType::i64) {
            return Ort::Value::CreateTensor<int64_t>(memInfo,
                                                     reinterpret_cast<int64_t *>(getData()),
                                                     getElementCount(),
                                                     onnxShape,
                                                     this->shape.dimensionCount);
        } else {
            assert(0);
        }
    }

  private:
    amp::ValueType type;
    size_t typeByteSize;
    amp::Shape shape;
    int64_t onnxShape[8];

    std::vector<uint8_t> data;
};
} // namespace onnx
