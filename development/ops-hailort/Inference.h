/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include <hailo/hailort.hpp>

#include "amp/Model.h"
#include "amp/ModelDescriptor.h"
#include "amp/Result.h"
#include "amp/Shape.h"

namespace hailort {

struct Inference {

    Inference();
    virtual ~Inference();

    amp::Result<void> setupFromJson(const std::string &filePath);
    amp::Result<void> setup(const ModelDescriptor &modelDesc);

    amp::Result<void>
    inference(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    const amp::Model &getModel() const {
        return this->model;
    }

    uint8_t *getInputTensorDataAddress(size_t index) {
        assert(index < amp::MaxTensorCount);
        assert(inputBuffers[index].data);

        return inputBuffers[index].data.get();
    }

    const uint8_t *getOutputTensorDataAddress(size_t index) const {
        assert(outputTensorPointers[index]);

        return outputTensorPointers[index];
    }

    amp::Shape getOutputTensorFinalShape(size_t index) const {
        assert(index < amp::MaxTensorCount);

        return outputTensorFinalShapes[index];
    }

  private:
    struct Buffer {
        std::shared_ptr<uint8_t> data;
        size_t byteCount = 0;
    };

    static amp::Result<hailo_format_type_t> ampTypeToHailoType(amp::Tdt type);
    static amp::Result<amp::Tdt> hailoTypeToAmpType(hailo_format_type_t type);
    static amp::Result<amp::Shape> hailoVstreamToAmpSize(const hailo_vstream_info_t &info,
                                                         size_t batchSize);

    static Buffer allocateBuffer(size_t byteCount);

    bool setupReady = false;
    ModelDescriptor modelDescriptor;
    amp::Model model;

    std::shared_ptr<hailort::VDevice> vdevice;
    std::shared_ptr<hailort::InferModel> inferModel;
    std::unique_ptr<hailort::ConfiguredInferModel> configuredInferModel;
    std::unique_ptr<hailort::ConfiguredInferModel::Bindings> bindings;

    Buffer inputBuffers[amp::MaxTensorCount];
    Buffer outputBuffers[amp::MaxTensorCount];

    const uint8_t *outputTensorPointers[amp::MaxTensorCount] = {nullptr};
    amp::Shape outputTensorFinalShapes[amp::MaxTensorCount];
};

} // namespace hailort
