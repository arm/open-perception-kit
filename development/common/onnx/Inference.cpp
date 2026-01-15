#include "Inference.h"

#include "amp/DescriptorStrings.h"
#include "amp/Result.h"
#include "amp/String.h"

#include "fmt/base.h"
#include "gst/video/video-enumtypes.h"
#include "onnx/Tools.h"
#include "onnxruntime_cxx_api.h"
#include "tl/expected.hpp"
#include <memory>

#include <fmt/core.h>

#include "postproc/PaddleocrParser.h"
#include "postproc/UltrafaceParser.h"
#include "postproc/YoloParser.h"

#include "preproc/ImageTensorBuilder.h"

#include "amp/Result.h"
#include "amp/String.h"
#include "amp/TensorInOut.h"
#include "amp/Types.h"

#include "magic_enum/magic_enum.hpp"

#include "ModelDescriptor.h"

using namespace onnx;

Inference::Inference() {}

Inference::~Inference() {
    if (this->session)
        delete this->session;
}

amp::Result<void> Inference::setupFromJson(const std::string &filePath) {

    auto descResult = ModelDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected{descResult.error()};
    }

    { // setup model file name
        std::string modelRoot = filePath;
        if (amp::utf8::contains(modelRoot, '/')) {
            size_t lastSlashAt = amp::utf8::lastIndexOf(modelRoot, '/');
            modelRoot = amp::utf8::left(modelRoot, lastSlashAt + 1);
        } else {
            modelRoot = "";
        }
        (*descResult).modelFile = modelRoot + (*descResult).modelFile;
    }

    auto setupResult = setup(*descResult);
    if (!setupResult) {
        return tl::unexpected{setupResult.error()};
    }

    return {};
}

amp::Result<void> Inference::setup(const ModelDescriptor &modelDesc_) {

    this->api = ApiTensorGlue();
    this->modelDescriptor = modelDesc_;
    // this->modelPath = file;

    try {

        this->sessionOptions = new Ort::SessionOptions();
        this->sessionOptions->SetIntraOpNumThreads(1);

        this->environment = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "ampinfer");
        this->memoryInfo =
            new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));

        this->session = new Ort::Session(
            *this->environment, modelDescriptor.modelFile.c_str(), *this->sessionOptions);

        auto modelResult = onnx::Tools::inspectModel(*this->session);
        if (!modelResult) {
            return tl::unexpected{modelResult.error()};
        }
        this->model = *modelResult;

        // --- build up model

        std::string modelLog = onnx::Tools::toString(this->model);
        printf("========= Original onnx model ========\n");
        printf("%s", modelLog.c_str());
        printf("========= ================== =========\n");

        auto cmResult = this->createModelFromModelDesc();
        if (!cmResult) {
            return tl::make_unexpected(cmResult.error());
        }

        this->setupTensorsForModel();

        auto ctpResult = this->createTensorProcessors();
        if (!ctpResult) {
            return tl::make_unexpected(ctpResult.error());
        }

        this->setupReady = true;

        // ---

        modelLog = onnx::Tools::toString(this->model);
        printf("======= Model updated with json ======\n");
        printf("%s", modelLog.c_str());
        printf("========= ================== =========\n");

    } catch (const std::exception &e) {
        return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::OnnxModelLoadException, e.what()));
    }

    return {};
}

amp::Result<void> Inference::createModelFromModelDesc() {

    this->model.modelFamily = this->modelDescriptor.modelFamily;

    // INPUT tensors
    if (this->model.modelInputCount != this->modelDescriptor.inputTensors.size()) {
        return tl::make_unexpected(AMP_ERROR(
            amp::ErrorFlag::InvalidData, "input tensor count must be the same in ONNX and json"));
    }

    for (size_t i = 0; i < this->modelDescriptor.inputTensors.size(); i++) {
        TensorDescriptor &descTensor = this->modelDescriptor.inputTensors[i];

        // setup data kind
        if (descTensor.dataKind == amp::TensorDataKind::Unknown) {
            return tl::make_unexpected(
                AMP_ERROR(amp::ErrorFlag::InvalidData, "input tensor data kind is unknown"));
        }
        this->model.inputs[i].dataKind = descTensor.dataKind;

        // check Value/Vector2/Vector3/Vector4 value count
        if (amp::isScalarDataKind(this->model.inputs[i].dataKind)) {
            if (this->modelDescriptor.inputTensors[i].shape.isValid()) {
                return tl::make_unexpected(AMP_ERROR(
                    amp::ErrorFlag::InvalidData,
                    "please do not include shape for Value/Vector input tensors in json"));
            }

            if ((this->model.inputs[i].dataKind == amp::TensorDataKind::Value &&
                 descTensor.valueInputs.size() != 1) ||
                (this->model.inputs[i].dataKind == amp::TensorDataKind::Vector2 &&
                 descTensor.valueInputs.size() != 2) ||
                (this->model.inputs[i].dataKind == amp::TensorDataKind::Vector3 &&
                 descTensor.valueInputs.size() != 3) ||
                (this->model.inputs[i].dataKind == amp::TensorDataKind::Vector4 &&
                 descTensor.valueInputs.size() != 4)) {
                return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                                     "input tensor Value/Vector needs the proper "
                                                     "amount of input valuea int valueInputs"));
            }
        } else {
            if (this->modelDescriptor.inputTensors[i].shape.isInvalid()) {
                return tl::make_unexpected(
                    AMP_ERROR(amp::ErrorFlag::InvalidData,
                              "please include shape for non-Value/Vector input tensors in json"));
            }
        }

        // setup final tenshor shape
        if (descTensor.shape.hasDynamicDimension()) {
            return tl::make_unexpected(
                AMP_ERROR(amp::ErrorFlag::InvalidData, "json cannot contain dynamic input shapes"));
        }

        if (this->model.inputs[i].shape.hasDynamicDimension()) {
            // if there is dynamic shape in onnx, the desc shape must be forced to it
            if (false == this->model.inputs[i].shape.applyDimensionsForDynamic(descTensor.shape)) {
                return tl::make_unexpected(
                    AMP_ERROR(amp::ErrorFlag::InvalidData,
                              "cannot apply json input tensor shape to onnx tensor shape"));
            }
        } else {
            // if no dynamic shape in onnx, but shape is provided in json -> they must match
            if (this->modelDescriptor.inputTensors[i].shape.isValid()) {
                if (this->modelDescriptor.inputTensors[i].shape == this->model.inputs[i].shape) {
                } else {
                    return tl::make_unexpected(
                        AMP_ERROR(amp::ErrorFlag::InvalidData,
                                  "if shape is provided in input tensor, the onnx static shape "
                                  "must mach, tip: you can skip shape in this case"));
                }
            }
        }
    }

    // OUTPUT tensors
    if (false == this->modelDescriptor.dynamicOutput) {
        if (this->model.modelOutputCount != this->modelDescriptor.outputTensors.size()) {
            return tl::make_unexpected(
                AMP_ERROR(amp::ErrorFlag::InvalidData,
                          "output tensor count must be the same in ONNX and json"));
        }
        if (this->model.modelInputCount != this->modelDescriptor.inputTensors.size()) {
            return tl::make_unexpected(
                AMP_ERROR(amp::ErrorFlag::InvalidData,
                          "output tensor count must be the same in ONNX and json"));
        }
    } else {
        if (this->modelDescriptor.outputTensors.size()) {
            return tl::make_unexpected(AMP_ERROR(
                amp::ErrorFlag::InvalidData,
                "please avoid to insert outputs in the json if the output is set to dynamic"));
        }
    }

    for (size_t i = 0; i < this->modelDescriptor.outputTensors.size(); i++) {
        TensorDescriptor &descTensor = this->modelDescriptor.outputTensors[i];

        // setup data kind
        if (descTensor.dataKind == amp::TensorDataKind::Unknown) {
            return tl::make_unexpected(
                AMP_ERROR(amp::ErrorFlag::InvalidData, "output tensor data kind is unknown"));
        }

        // setup final tenshor shape
        if (descTensor.shape.hasDynamicDimension()) {
            return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                                 "json cannot contain dynamic output shapes"));
        }

        if (this->model.outputs[i].shape.hasDynamicDimension()) {
            if (false == this->model.outputs[i].shape.applyDimensionsForDynamic(descTensor.shape)) {
                return tl::make_unexpected(
                    AMP_ERROR(amp::ErrorFlag::InvalidData,
                              "cannot apply json output tensor shape to onnx tensor shape"));
            }
        } else {
            // if no dynamic shape in onnx, but shape is provided in dest, they must match
            if (this->modelDescriptor.outputTensors[i].shape.isInvalid()) {
                if (this->modelDescriptor.outputTensors[i].shape != this->model.outputs[i].shape) {
                    return tl::make_unexpected(
                        AMP_ERROR(amp::ErrorFlag::InvalidData,
                                  "if shape is provided in output tensor, the onnx static shape "
                                  "must mach, tip: you can skip shape in this case"));
                }
            }
        }
    }

    // other stuff
    this->useDynamicOutput = this->modelDescriptor.dynamicOutput;

    return {};
}

void Inference::setupTensorsForModel() {

    for (size_t i = 0; i < this->model.modelInputCount; i++) {
        api.inputTensors[i] = std::make_unique<onnx::Tensor>(this->model.inputs[i].shape,
                                                             this->model.inputs[i].valueType);
        api.inputNames.push_back(this->model.inputs[i].name.c_str());
        api.inputTensorVector.push_back(api.inputTensors[i]->createOnnxTensor(*this->memoryInfo));
    }

    for (size_t i = 0; i < this->model.modelOutputCount; i++) {
        api.outputTensors[i] = std::make_unique<onnx::Tensor>(this->model.outputs[i].shape,
                                                              this->model.outputs[i].valueType);
        api.outputNames.push_back(this->model.outputs[i].name.c_str());
        if (false == this->useDynamicOutput)
            api.outputTensorVector.push_back(
                api.outputTensors[i]->createOnnxTensor(*this->memoryInfo));
    }

    fmt::print("Input tensors are set up\n");
}

amp::Result<void> Inference::createTensorProcessors() {

    if (this->modelDescriptor.modelFamily == amp::NetworkId::YoloObjectDetection) {
        this->outputParser = std::make_unique<amp::YoloLikeParser>();
        fmt::print("Creating tensor parser: YoloLikeParser\n");
    } else if (this->modelDescriptor.modelFamily == amp::NetworkId::UltraFace) {
        this->outputParser = std::make_unique<amp::UltraFaceParser>();
        fmt::print("Creating tensor parser: UltraFaceParser\n");
    } else if (this->modelDescriptor.modelFamily == amp::NetworkId::PaddleOcrDetection) {
        this->outputParser = std::make_unique<amp::PaddleOcrDetectionParser>();
        fmt::print("Creating tensor parser: PaddleOcrDetectionParser\n");
    } else {
        return tl::make_unexpected(
            AMP_ERROR(amp::ErrorFlag::NotSupported,
                      fmt::format("cannot create output tensor parser for [{}]",
                                  this->modelDescriptor.modelFamily)));
    }

    this->inputBuilder = std::make_unique<amp::ImageTensorBuilder>();

    return {};
}

amp::Result<void> Inference::preprocessImageData(size_t tensorIndex,
                                                 const uint8_t *data,
                                                 amp::TensorDataKind dataKind,
                                                 amp::ValueType valueType,
                                                 size_t imageWidth,
                                                 size_t imageHeight) {

    amp::NetworkInputBuilder::Setup setup;
    setup.original.data = data;
    setup.original.width = imageWidth;
    setup.original.height = imageHeight;
    setup.original.byteCount = imageWidth * imageHeight * 3;
    setup.original.kind = dataKind;
    setup.original.type = valueType;

    size_t modelWidth, modelHeight;
    if (false == model.inputs[tensorIndex].tryGetImageTensorSize(modelWidth, modelHeight)) {
        return tl::make_unexpected(
            AMP_ERROR(amp::ErrorFlag::InvalidData, "tensor seems not to be an image"));
    }

    setup.target.data = api.inputTensors[tensorIndex]->getData();
    setup.target.byteCount = api.inputTensors[tensorIndex]->getByteCount();
    setup.target.width = modelWidth;
    setup.target.height = modelHeight;
    setup.target.kind = model.inputs[0].dataKind;
    setup.target.type = amp::ValueType::f32;

    amp::Result<void> result = inputBuilder->build(setup);
    if (result.has_value() == false) {
        return result;
        // return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData, "tensor build error"));
    }

    this->inferenceMetaData.image.width = imageWidth;
    this->inferenceMetaData.image.height = imageHeight;
    this->inferenceMetaData.image.modelWidth = modelWidth;
    this->inferenceMetaData.image.modelHeight = modelHeight;

    return {};
}

template <typename toT, typename fromT>
void writeValueTo(void *ptr, size_t valueIndex, void *valueAddress) {
    toT *address = (toT *)ptr;
    address[valueIndex] = *(fromT *)valueAddress;
}

amp::Result<void> Inference::inference() {

    // setting scalar tensors
    for (size_t i = 0; i < api.inputTensorVector.size(); i++) {
        if (amp::isScalarDataKind(this->model.inputs[i].dataKind)) {

            size_t valueCount = 0;
            if (this->model.inputs[i].dataKind == amp::TensorDataKind::Value)
                valueCount = 1;
            if (this->model.inputs[i].dataKind == amp::TensorDataKind::Vector2)
                valueCount = 2;
            if (this->model.inputs[i].dataKind == amp::TensorDataKind::Vector3)
                valueCount = 3;
            if (this->model.inputs[i].dataKind == amp::TensorDataKind::Vector4)
                valueCount = 4;

            if (this->model.inputs[i].valueType == amp::ValueType::f32) {
                for (size_t g = 0; g < valueCount; g++) {
                    writeValueTo<float, float>(
                        this->modelDescriptor.inputTensors[i].valueInputs.data(),
                        g,
                        api.inputTensors[i]->getData());
                }
            } else {
                assert(0); // no type support to set scalar tensor input value
            }
        }
    }

    /*if(api.inputTensorVector.size() > 1) {
        *(float*)api.inputTensors[1]->getData() = 0.2f; // confidence
        *(int64_t*)api.inputTensors[2]->getData() = 5; // numdetections
        *(float*)api.inputTensors[3]->getData() = 0.5f; // iou threshold
    }*/

    // run the inference
    try {
        if (false == this->useDynamicOutput) {
            this->session->Run(Ort::RunOptions{nullptr},
                               (const char *const *)this->api.inputNames.data(),
                               api.inputTensorVector.data(),
                               api.inputTensorVector.size(),
                               (const char *const *)api.outputNames.data(),
                               api.outputTensorVector.data(),
                               api.outputTensorVector.size());
        } else {
            dynamicOutputData = this->session->Run(Ort::RunOptions{nullptr},
                                                   (const char *const *)api.inputNames.data(),
                                                   api.inputTensorVector.data(),
                                                   api.inputTensorVector.size(),
                                                   (const char *const *)api.outputNames.data(),
                                                   api.outputNames.size());
        }
    } catch (const std::exception &e) {
        return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::OnnxInferenceException, e.what()));
    }

    return {};
}

amp::Result<void> Inference::postprocess(const amp::NetworkOutputParser::Settings &settings,
                                         amp::DetectionResult &outDetectionResults) {

    if (!this->useDynamicOutput) {
        // --- STATIC, PREALLOCATED OUTPUTS ---

        const amp::TensorReader *tensorReaders[4] = {nullptr, nullptr, nullptr, nullptr};

        for (size_t i = 0; i < 4; ++i) {
            if (i < model.modelOutputCount) {
                if (this->outputTensorReaders[i] == nullptr) {
                    this->outputTensorReaders[i] =
                        std::make_unique<amp::TensorReader>(api.outputTensors[i]->getData(),
                                                            api.outputTensors[i]->getByteCount(),
                                                            model.outputs[i].shape,
                                                            model.outputs[i].valueType,
                                                            1.0f,
                                                            0.0f);
                }
                tensorReaders[i] = this->outputTensorReaders[i].get();
            }
        }

        amp::Result<void> inferenceResult = outputParser->parse(
            tensorReaders, settings, this->inferenceMetaData, outDetectionResults);
        if (inferenceResult.has_value() == false)
            return inferenceResult;

    } else {
        // --- DYNAMIC OUTPUTS ALLOCATED BY ORT ---

        const amp::TensorReader *tensorReaders[4] = {nullptr, nullptr, nullptr, nullptr};

        // We’ll build temporary TensorReaders for this call only.
        // They just wrap ORT’s output buffers; no copying.
        std::vector<std::unique_ptr<amp::TensorReader>> dynamicReaders;
        dynamicReaders.resize(4);

        const size_t numOutputs =
            std::min<size_t>(std::min<size_t>(model.modelOutputCount, dynamicOutputData.size()), 4);

        for (size_t i = 0; i < numOutputs; ++i) {
            Ort::Value &v = dynamicOutputData[i];

            auto tinfo = v.GetTensorTypeAndShapeInfo();
            auto onnxShape = tinfo.GetShape();
            ONNXTensorElementDataType elemType = tinfo.GetElementType();

            // Map ONNX type → amp::ValueType
            amp::ValueType valueType;
            if (!onnx::Tools::onnxTypeToUniflowType(elemType, valueType)) {
                // If you have better error handling, plug it here
                assert(0);
            }

            // Build amp::Shape from ORT shape
            amp::Shape shape;
            shape.dimensionCount = onnxShape.size();
            for (size_t d = 0; d < shape.dimensionCount; ++d) {
                shape.valueCount[d] = static_cast<size_t>(onnxShape[d]);
            }

            // Compute byte count
            const size_t elemSize = amp::getValueTypeByteSize(valueType);
            const size_t byteCount = elemSize * shape.getFullValueCount();

            // Get raw data pointer from ORT tensor
            void *dataPtr = v.GetTensorMutableData<void>();

            dynamicReaders[i] = std::make_unique<amp::TensorReader>(
                dataPtr, byteCount, shape, valueType, 1.0f, 0.0f);

            tensorReaders[i] = dynamicReaders[i].get();
        }

        amp::Result<void> inferenceResult = outputParser->parse(
            tensorReaders, settings, this->inferenceMetaData, outDetectionResults);
        if (inferenceResult.has_value() == false)
            return inferenceResult;
        // dynamicReaders stays alive until here, so tensorReaders are valid during parse()
    }

    return {};
}
