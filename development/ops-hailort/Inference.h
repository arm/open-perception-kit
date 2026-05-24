/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include <hailo/hailort.hpp>

#include "pek/Model.h"
#include "pek/ModelDescriptor.h"
#include "pek/Result.h"
#include "pek/Shape.h"

namespace hailort {

struct Inference {

    Inference();
    virtual ~Inference();

    pek::Result<void> setupFromJson(const std::string &filePath);
    pek::Result<void> setup(const ModelDescriptor &modelDesc);

    pek::Result<void>
    inference(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    const pek::Model &getModel() const {
        return this->model;
    }

    uint8_t *getInputTensorDataAddress(size_t index) {
        assert(index < pek::MaxTensorCount);
        assert(inputBuffers[index].data);

        return inputBuffers[index].data.get();
    }

    const uint8_t *getOutputTensorDataAddress(size_t index) const {
        assert(outputTensorPointers[index]);

        return outputTensorPointers[index];
    }

    pek::Shape getOutputTensorFinalShape(size_t index) const {
        assert(index < pek::MaxTensorCount);

        return outputTensorFinalShapes[index];
    }

  private:
    struct Buffer {
        std::shared_ptr<uint8_t> data;
        size_t byteCount = 0;
    };

    static pek::Result<hailo_format_type_t> pekTypeToHailoType(pek::Dtype type);
    static pek::Result<pek::Dtype> hailoTypeToPekType(hailo_format_type_t type);
    static pek::Result<pek::Shape> hailoVstreamToPekSize(const hailo_vstream_info_t &info,
                                                         size_t batchSize);

    static Buffer allocateBuffer(size_t byteCount);

    bool setupReady = false;
    ModelDescriptor modelDescriptor;
    pek::Model model;

    std::shared_ptr<hailort::VDevice> vdevice;
    std::shared_ptr<hailort::InferModel> inferModel;
    std::unique_ptr<hailort::ConfiguredInferModel> configuredInferModel;
    std::unique_ptr<hailort::ConfiguredInferModel::Bindings> bindings;

    Buffer inputBuffers[pek::MaxTensorCount];
    Buffer outputBuffers[pek::MaxTensorCount];

    const uint8_t *outputTensorPointers[pek::MaxTensorCount] = {nullptr};
    pek::Shape outputTensorFinalShapes[pek::MaxTensorCount];
};

} // namespace hailort
