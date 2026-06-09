/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "pek/Model.h"

namespace pek {

bool ModelInput::tryGetImageTensorSize(size_t &outWidth, size_t &outHeight) {
    if (shape.rank == 4) {
        if (shape.dims[1] == 1 || shape.dims[1] == 3) {
            outWidth = shape.dims[3];
            outHeight = shape.dims[2];
            return true;
        }
        if (shape.dims[3] == 1 || shape.dims[3] == 3) {
            outWidth = shape.dims[2];
            outHeight = shape.dims[1];
            return true;
        }
    }
    return false;
}

TensorView Model::createOutputTensorView(size_t index, const uint8_t *data) const {
    assert(index < outputs.size());

    TensorView tw(data,
                  outputs[index].shape.getFullValueCount() *
                      pek::getValueTypeByteSize(outputs[index].valueType),
                  outputs[index].shape,
                  outputs[index].valueType,
                  outputs[index].quantArguments.scale,
                  outputs[index].quantArguments.zeroPoint);

    return tw;
}

TensorView
Model::createOutputTensorView(size_t index, const uint8_t *data, const pek::Shape &shape) const {
    assert(index < outputs.size());

    TensorView tw(data,
                  shape.getFullValueCount() * pek::getValueTypeByteSize(outputs[index].valueType),
                  shape,
                  outputs[index].valueType,
                  outputs[index].quantArguments.scale,
                  outputs[index].quantArguments.zeroPoint);

    return tw;
}

pek::Result<void> Model::applyModelFromDescriptor(const ModelDescriptor &modelDescriptor) {

    this->modelFamily = modelDescriptor.modelFamily;
    this->contentType = modelDescriptor.contentType;

    // INPUT tensors
    if (inputs.size() != modelDescriptor.inputTensors.size()) {
        return tl::make_unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      "input tensor count must be the same in runtime model and json"));
    }

    for (size_t i = 0; i < modelDescriptor.inputTensors.size(); i++) {
        const TensorDescriptor &descTensor = modelDescriptor.inputTensors[i];

        // setup data kind
        if (descTensor.dataKind == pek::DataKind::Unknown) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData, "input tensor data kind is unknown"));
        }
        this->inputs[i].dataKind = descTensor.dataKind;

        // check Value/Vector2/Vector3/Vector4 value count
        if (pek::isScalarDataKind(this->inputs[i].dataKind)) {
            if (modelDescriptor.inputTensors[i].shape.isValid()) {
                return tl::make_unexpected(PEK_ERROR(
                    pek::ErrorFlag::InvalidData,
                    "please do not include shape for Value/Vector input tensors in json"));
            }

            if ((this->inputs[i].dataKind == pek::DataKind::Value &&
                 descTensor.valueInputs.size() != 1) ||
                (this->inputs[i].dataKind == pek::DataKind::Vector2 &&
                 descTensor.valueInputs.size() != 2) ||
                (this->inputs[i].dataKind == pek::DataKind::Vector3 &&
                 descTensor.valueInputs.size() != 3) ||
                (this->inputs[i].dataKind == pek::DataKind::Vector4 &&
                 descTensor.valueInputs.size() != 4)) {
                return tl::make_unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData,
                                                     "input tensor Value/Vector needs the proper "
                                                     "amount of input values in valueInputs"));
            }
        } else {
            if (modelDescriptor.inputTensors[i].shape.isInvalid()) {
                return tl::make_unexpected(
                    PEK_ERROR(pek::ErrorFlag::InvalidData,
                              "please include shape for non-Value/Vector input tensors in json"));
            }
        }

        // setup final tensor shape
        if (descTensor.shape.hasDynamicDimension()) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData, "json cannot contain dynamic input shapes"));
        }

        if (this->inputs[i].shape.hasDynamicDimension()) {
            // if there is dynamic shape in onnx, the desc shape must be forced to it
            if (false == this->inputs[i].shape.applyDimensionsForDynamic(descTensor.shape)) {
                return tl::make_unexpected(
                    PEK_ERROR(pek::ErrorFlag::InvalidData,
                              "cannot apply json input tensor shape to onnx tensor shape"));
            }
        } else {
            // if no dynamic shape in onnx, but shape is provided in json -> they must match
            if (modelDescriptor.inputTensors[i].shape.isValid()) {
                if (modelDescriptor.inputTensors[i].shape == this->inputs[i].shape) {
                } else {
                    return tl::make_unexpected(
                        PEK_ERROR(pek::ErrorFlag::InvalidData,
                                  "if shape is provided in input tensor, the runtime static shape "
                                  "must match, tip: you can skip shape in this case"));
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
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          "output tensor count must be the same in runtime model and json"));
        }
        if (inputs.size() != modelDescriptor.inputTensors.size()) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          "input tensor count must be the same in runtime model and json"));
        }
    } else {
        if (modelDescriptor.outputTensors.size()) {
            return tl::make_unexpected(PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                "please avoid to insert outputs in the json if the output is set to dynamic"));
        }
    }

    for (size_t i = 0; i < modelDescriptor.outputTensors.size(); i++) {
        const TensorDescriptor &descTensor = modelDescriptor.outputTensors[i];

        // setup data kind
        if (descTensor.dataKind == pek::DataKind::Unknown) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData, "output tensor data kind is unknown"));
        }

        // setup final tensor shape
        if (descTensor.shape.hasDynamicDimension()) {
            return tl::make_unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData,
                                                 "json cannot contain dynamic output shapes"));
        }

        if (this->outputs[i].shape.hasDynamicDimension()) {
            if (false == this->outputs[i].shape.applyDimensionsForDynamic(descTensor.shape)) {
                return tl::make_unexpected(
                    PEK_ERROR(pek::ErrorFlag::InvalidData,
                              "cannot apply json output tensor shape to onnx tensor shape"));
            }
        } else {
            // if no dynamic shape in onnx, but shape is provided in dest, they must match
            if (modelDescriptor.outputTensors[i].shape.isValid()) {
                if (modelDescriptor.outputTensors[i].shape != this->outputs[i].shape) {
                    return tl::make_unexpected(
                        PEK_ERROR(pek::ErrorFlag::InvalidData,
                                  "if shape is provided in output tensor, the runtime static shape "
                                  "must match, tip: you can skip shape in this case"));
                }
            }
        }
    }

    // other stuff
    this->useDynamicOutput = modelDescriptor.dynamicOutput;
    this->tensorFeedbacks = modelDescriptor.tensorFeedbacks;

    // check tensor feedbacks
    if (this->useDynamicOutput == false) {
        for (const auto &input : inputs) {
            if (input.matchShapeOutputIndex != pek::InvalidTensorIndex) {
                return tl::make_unexpected(
                    PEK_ERROR(pek::ErrorFlag::InvalidData,
                              "matchShapeOutputIndex cannot be used if the output is not dynamic"));
            }
        }
    }

    return {};
}

std::string Model::toString() const {
    std::string ret;

    ret += fmt::format("Model: [{}]\n", modelFamily);
    ret += fmt::format("Engine: [{}]\n", engine);
    ret += fmt::format("Input count: {}\n", inputs.size());
    ret += fmt::format("Output count: {}\n", outputs.size());
    ret += "\n";

    for (size_t i = 0; i < inputs.size(); i++) {
        ret += fmt::format("Input #{} [{}]\n", i, inputs[i].name);
        ret += fmt::format(" Batch {}\n", inputs[i].batch);
        ret += fmt::format(" ValueType: {}\n", magic_enum::enum_name(inputs[i].valueType));
        ret += fmt::format(" Shape: {}\n", inputs[i].shape.toString());
        ret += fmt::format(" DataKind: {}\n", magic_enum::enum_name(inputs[i].dataKind));
    }
    ret += "\n";

    for (size_t i = 0; i < outputs.size(); i++) {
        ret += fmt::format("Output #{} [{}]\n", i, outputs[i].name);
        ret += fmt::format(" ValueType: {}\n", magic_enum::enum_name(outputs[i].valueType));
        ret += fmt::format(" Shape: {}\n", outputs[i].shape.toString());
    }
    ret += "\n";

    return ret;
}

} // namespace pek
