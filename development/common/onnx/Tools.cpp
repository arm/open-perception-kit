#include "Tools.h"

#include "magic_enum/magic_enum.hpp"
#include "onnxruntime_c_api.h"
#include "onnxruntime_cxx_api.h"
#include "tl/expected.hpp"

#include "amp/Shape.h"
#include "amp/Types.h"

#include <fmt/core.h>

#include "amp/Result.h"

using namespace onnx;

bool Tools::onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, amp::ValueType &outType) {
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        outType = amp::ValueType::f32;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        outType = amp::ValueType::f16;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
        outType = amp::ValueType::i8;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8) {
        outType = amp::ValueType::u8;
        return true;
    }
    if (onnxType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        outType = amp::ValueType::i64;
        return true;
    }
    return false;
}

std::vector<size_t>
Tools::getTensorShape(const Ort::Session &session, amp::TensorInOut tensorInOut, int tensorIndex) {
    Ort::TypeInfo ti = (tensorInOut == amp::TensorInOut::In)
                           ? session.GetInputTypeInfo(tensorIndex)
                           : session.GetOutputTypeInfo(tensorIndex);
    auto tensor = ti.GetTensorTypeAndShapeInfo();
    std::vector<size_t> dims;
    for (const auto &a : tensor.GetShape())
        dims.push_back(a);
    return dims;
}

amp::Result<amp::Model> Tools::inspectModel(const Ort::Session &session) {

    amp::Model model;

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
        amp::ValueType tensorValueType;
        if (false == onnxTypeToUniflowType(tensor.GetElementType(), tensorValueType)) {
            return tl::unexpected{AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                                            fmt::format("cannot recognize input ONNX type: {}",
                                                        (uint64_t)tensor.GetElementType()))};
        }

        if (amp::ValueType::f32 != tensorValueType && amp::ValueType::i64 != tensorValueType) {
            return tl::unexpected{
                AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                          "only float32 or int64 input tensors are supported in ONNX")};
        }
        model.inputs[i].valueType = tensorValueType;

        // shape
        std::vector<size_t> onnxDims = getTensorShape(session, amp::TensorInOut::In, i);
        if (onnxDims.size() < 1 || onnxDims.size() > 8) {
            return tl::unexpected{AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                                            "input tensor size must be between 1 and 8")};
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
        amp::ValueType tensorValueType;
        if (false == onnxTypeToUniflowType(tensor.GetElementType(), tensorValueType)) {
            return tl::unexpected{AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                                            fmt::format("cannot recognize output ONNX type: {}",
                                                        (uint64_t)tensor.GetElementType()))};
        }

        if (amp::ValueType::f32 != tensorValueType && amp::ValueType::i64 != tensorValueType) {
            return tl::unexpected{
                AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                          "only float32 or int64 input tensors are supported in ONNX")};
        }
        model.outputs[i].valueType = tensorValueType;

        // shape
        std::vector<size_t> onnxDims = getTensorShape(session, amp::TensorInOut::Out, i);
        if (onnxDims.size() < 1 || onnxDims.size() > 8) {
            return tl::unexpected{AMP_ERROR(amp::ErrorFlag::ModelInspectError,
                                            "output tensor size must be between 1 and 8")};
        }
        model.outputs[i].shape.setFrom(onnxDims);
        fmt::print("Output shape {}\n", model.outputs[i].shape.toString().c_str());
    }

    return model;
}

std::string Tools::toString(const amp::Model &model) {
    std::string ret;

    ret += fmt::format("Model: [{}]\n", model.modelFamily.c_str());
    ret += fmt::format("Input count: {}\n", model.modelInputCount);
    ret += fmt::format("Output count: {}\n", model.modelOutputCount);

    for (size_t i = 0; i < model.modelInputCount; i++) {
        ret += fmt::format("Input #{} [{}]\n", i, model.inputs[i].name.c_str());
        ret += fmt::format(" Batch {}\n", model.inputs[i].batch);
        ret += fmt::format(" ValueType: {}\n", magic_enum::enum_name(model.inputs[i].valueType));
        ret += fmt::format(" Shape: {}\n", model.inputs[i].shape.toString().c_str());
        // ret += fmt::format(" Shape: {}\n", amp::toString(model.inputs[i].shape).c_str());
        ret += fmt::format(" DataKind: {}\n", magic_enum::enum_name(model.inputs[i].dataKind));
    }
    for (size_t i = 0; i < model.modelOutputCount; i++) {
        ret += fmt::format("Output #{} [{}]\n", i, model.outputs[i].name.c_str());
        ret += fmt::format(" ValueType: {}\n", magic_enum::enum_name(model.outputs[i].valueType));
        // ret += fmt::format(" Shape: {}\n", amp::toString(model.outputs[i].shape).c_str());
        ret += fmt::format(" Shape: {}\n", model.outputs[i].shape.toString().c_str());
    }

    return ret;
}
