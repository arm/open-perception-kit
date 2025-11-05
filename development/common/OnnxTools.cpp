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

