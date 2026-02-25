/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/ModelDescriptor.h"
#include "amp/Result.h"
#include "amp/Shape.h"
#include "amp/TensorView.h"
#include "amp/Types.h"

#include "fmt/format.h"
#include "magic_enum/magic_enum.hpp"
#include <vector>

namespace amp {

struct ModelInput {
    std::string name;
    amp::DataKind dataKind = amp::DataKind::Unknown;
    amp::Tdt valueType = amp::Tdt::Float32;
    amp::Shape shape{};
    int batch = 0;
    amp::QuantizationArgs quantArguments;
    amp::Colorf mean = {0.0f, 0.0f, 0.0f, 0.0f}, std = {1.0f, 1.0f, 1.0f, 1.0f};

    std::vector<float> valueInputs;

    // if this value is not amp::InvalidTensorIndex
    // we have to realloc the tensor to match the shape of the referenced output tensor
    size_t matchShapeOutputIndex = amp::InvalidTensorIndex;

    bool tryGetImageTensorSize(size_t &outWidht, size_t &outHeight) {
        if (shape.dimensionCount == 4) {
            if (shape.valueCount[1] == 1 || shape.valueCount[1] == 3) {
                outWidht = shape.valueCount[3];
                outHeight = shape.valueCount[2];
                return true;
            }
            if (shape.valueCount[3] == 1 || shape.valueCount[3] == 3) {
                outWidht = shape.valueCount[2];
                outHeight = shape.valueCount[1];
                return true;
            }
        }
        return false;
    }
};

struct ModelOutput {
    std::string name;
    amp::Tdt valueType = amp::Tdt::Float32;
    amp::Shape shape;
    amp::QuantizationArgs quantArguments;
};

struct Model {

    bool inputSizeAppliedByModel = false;
    bool nmsAppliedByModel = false;

    std::string modelFamily, engine, contentType;

    std::vector<ModelInput> inputs;

    std::vector<ModelOutput> outputs;

    std::vector<amp::TensorFeedback> tensorFeedbacks;

    // some runtimes enable models
    // where the output size is only determined
    // while executiong the inference
    bool useDynamicOutput = false;

    TensorView createOutputTensorView(size_t index, const uint8_t *data) const {
        assert(index < outputs.size());

        TensorView tw(data,
                      outputs[index].shape.getFullValueCount() *
                          amp::getValueTypeByteSize(outputs[index].valueType),
                      outputs[index].shape,
                      outputs[index].valueType,
                      outputs[index].quantArguments.scale,
                      outputs[index].quantArguments.zeroPoint);

        return tw;
    }

    TensorView
    createOutputTensorView(size_t index, const uint8_t *data, const amp::Shape &shape) const {
        assert(index < outputs.size());

        TensorView tw(data,
                      outputs[index].shape.getFullValueCount() *
                          amp::getValueTypeByteSize(outputs[index].valueType),
                      shape,
                      outputs[index].valueType,
                      outputs[index].quantArguments.scale,
                      outputs[index].quantArguments.zeroPoint);

        return tw;
    }

    // there are 2 models: data from the model file + data from the JSON file
    // this func must be called on the Model instance built from the model file
    // the argument is the ModelDescriptor instance from the JSON file
    // this tries to unify the two and create a final model
    amp::Result<void> applyModelFromDescriptor(const ModelDescriptor &modelDescriptor) {

        this->modelFamily = modelDescriptor.modelFamily;
        this->contentType = modelDescriptor.contentType;

        // INPUT tensors
        if (inputs.size() != modelDescriptor.inputTensors.size()) {
            return tl::make_unexpected(
                AMP_ERROR(amp::ErrorFlag::InvalidData,
                          "input tensor count must be the same in ONNX and json"));
        }

        for (size_t i = 0; i < modelDescriptor.inputTensors.size(); i++) {
            const TensorDescriptor &descTensor = modelDescriptor.inputTensors[i];

            // setup data kind
            if (descTensor.dataKind == amp::DataKind::Unknown) {
                return tl::make_unexpected(
                    AMP_ERROR(amp::ErrorFlag::InvalidData, "input tensor data kind is unknown"));
            }
            this->inputs[i].dataKind = descTensor.dataKind;

            // check Value/Vector2/Vector3/Vector4 value count
            if (amp::isScalarDataKind(this->inputs[i].dataKind)) {
                if (modelDescriptor.inputTensors[i].shape.isValid()) {
                    return tl::make_unexpected(AMP_ERROR(
                        amp::ErrorFlag::InvalidData,
                        "please do not include shape for Value/Vector input tensors in json"));
                }

                if ((this->inputs[i].dataKind == amp::DataKind::Value &&
                     descTensor.valueInputs.size() != 1) ||
                    (this->inputs[i].dataKind == amp::DataKind::Vector2 &&
                     descTensor.valueInputs.size() != 2) ||
                    (this->inputs[i].dataKind == amp::DataKind::Vector3 &&
                     descTensor.valueInputs.size() != 3) ||
                    (this->inputs[i].dataKind == amp::DataKind::Vector4 &&
                     descTensor.valueInputs.size() != 4)) {
                    return tl::make_unexpected(
                        AMP_ERROR(amp::ErrorFlag::InvalidData,
                                  "input tensor Value/Vector needs the proper "
                                  "amount of input valuea int valueInputs"));
                }
            } else {
                if (modelDescriptor.inputTensors[i].shape.isInvalid()) {
                    return tl::make_unexpected(AMP_ERROR(
                        amp::ErrorFlag::InvalidData,
                        "please include shape for non-Value/Vector input tensors in json"));
                }
            }

            // setup final tenshor shape
            if (descTensor.shape.hasDynamicDimension()) {
                return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                                     "json cannot contain dynamic input shapes"));
            }

            if (this->inputs[i].shape.hasDynamicDimension()) {
                // if there is dynamic shape in onnx, the desc shape must be forced to it
                if (false == this->inputs[i].shape.applyDimensionsForDynamic(descTensor.shape)) {
                    return tl::make_unexpected(
                        AMP_ERROR(amp::ErrorFlag::InvalidData,
                                  "cannot apply json input tensor shape to onnx tensor shape"));
                }
            } else {
                // if no dynamic shape in onnx, but shape is provided in json -> they must match
                if (modelDescriptor.inputTensors[i].shape.isValid()) {
                    if (modelDescriptor.inputTensors[i].shape == this->inputs[i].shape) {
                    } else {
                        return tl::make_unexpected(
                            AMP_ERROR(amp::ErrorFlag::InvalidData,
                                      "if shape is provided in input tensor, the onnx static shape "
                                      "must mach, tip: you can skip shape in this case"));
                    }
                }
            }

            // set mean and std
            this->inputs[i].mean = modelDescriptor.inputTensors[i].mean;
            this->inputs[i].std = modelDescriptor.inputTensors[i].std;
            this->inputs[i].valueInputs = modelDescriptor.inputTensors[i].valueInputs;
            this->inputs[i].matchShapeOutputIndex =
                modelDescriptor.inputTensors[i].matchShapeOutputIndex;
        }

        // OUTPUT tensors
        if (false == modelDescriptor.dynamicOutput) {
            if (outputs.size() != modelDescriptor.outputTensors.size()) {
                return tl::make_unexpected(
                    AMP_ERROR(amp::ErrorFlag::InvalidData,
                              "output tensor count must be the same in ONNX and json"));
            }
            if (inputs.size() != modelDescriptor.inputTensors.size()) {
                return tl::make_unexpected(
                    AMP_ERROR(amp::ErrorFlag::InvalidData,
                              "output tensor count must be the same in ONNX and json"));
            }
        } else {
            if (modelDescriptor.outputTensors.size()) {
                return tl::make_unexpected(AMP_ERROR(
                    amp::ErrorFlag::InvalidData,
                    "please avoid to insert outputs in the json if the output is set to dynamic"));
            }
        }

        for (size_t i = 0; i < modelDescriptor.outputTensors.size(); i++) {
            const TensorDescriptor &descTensor = modelDescriptor.outputTensors[i];

            // setup data kind
            if (descTensor.dataKind == amp::DataKind::Unknown) {
                return tl::make_unexpected(
                    AMP_ERROR(amp::ErrorFlag::InvalidData, "output tensor data kind is unknown"));
            }

            // setup final tenshor shape
            if (descTensor.shape.hasDynamicDimension()) {
                return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                                     "json cannot contain dynamic output shapes"));
            }

            if (this->outputs[i].shape.hasDynamicDimension()) {
                if (false == this->outputs[i].shape.applyDimensionsForDynamic(descTensor.shape)) {
                    return tl::make_unexpected(
                        AMP_ERROR(amp::ErrorFlag::InvalidData,
                                  "cannot apply json output tensor shape to onnx tensor shape"));
                }
            } else {
                // if no dynamic shape in onnx, but shape is provided in dest, they must match
                if (modelDescriptor.outputTensors[i].shape.isInvalid()) {
                    if (modelDescriptor.outputTensors[i].shape != this->outputs[i].shape) {
                        return tl::make_unexpected(AMP_ERROR(
                            amp::ErrorFlag::InvalidData,
                            "if shape is provided in output tensor, the onnx static shape "
                            "must mach, tip: you can skip shape in this case"));
                    }
                }
            }
        }

        // other stuff
        this->useDynamicOutput = modelDescriptor.dynamicOutput;
        this->tensorFeedbacks = modelDescriptor.tensorFeedbacks;

        // check tensor feedbacks
        for (const auto &input : inputs) {
            if (input.matchShapeOutputIndex != amp::InvalidTensorIndex &&
                this->useDynamicOutput == false) {
                return tl::make_unexpected(
                    AMP_ERROR(amp::ErrorFlag::InvalidData,
                              "matchShapeOutputIndex cannot be used if the output is not dynamic"));
            }
        }

        return {};
    }

    std::string toString() const {
        std::string ret;

        ret += fmt::format("Model: [{}]\n", modelFamily);
        ret += fmt::format("Engine: [{}]\n", engine);
        ret += fmt::format("Input count: {}\n", inputs.size());
        ret += fmt::format("Output count: {}\n", outputs.size());

        for (size_t i = 0; i < inputs.size(); i++) {
            ret += fmt::format("Input #{} [{}]\n", i, inputs[i].name);
            ret += fmt::format(" Batch {}\n", inputs[i].batch);
            ret += fmt::format(" ValueType: {}\n", magic_enum::enum_name(inputs[i].valueType));
            ret += fmt::format(" Shape: {}\n", inputs[i].shape.toString());
            // ret += fmt::format(" Shape: {}\n", amp::toString(model.inputs[i].shape).c_str());
            ret += fmt::format(" DataKind: {}\n", magic_enum::enum_name(inputs[i].dataKind));
        }
        for (size_t i = 0; i < outputs.size(); i++) {
            ret += fmt::format("Output #{} [{}]\n", i, outputs[i].name);
            ret += fmt::format(" ValueType: {}\n", magic_enum::enum_name(outputs[i].valueType));
            // ret += fmt::format(" Shape: {}\n",
            // amp::toString(model.outputs[i].shape).c_str());
            ret += fmt::format(" Shape: {}\n", outputs[i].shape.toString());
        }

        return ret;
    }
};

} // namespace amp
