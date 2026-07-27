/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <ncnn/net.h>

#include "pek/Model.h"
#include "pek/ModelDescriptor.h"
#include "pek/Result.h"
#include "pek/Shape.h"

namespace pek::ncnnrt {

struct Inference {

    Inference();
    virtual ~Inference();

    pek::Result<void> setup(const pek::ModelDescriptor &modelDesc);

    bool isReady() const {
        return setupReady;
    }

    pek::Result<void> inference();

    const pek::Model &getModel() const {
        return model;
    }

    uint8_t *getInputTensorDataAddress(size_t index) {
        return inputTensors[index].data();
    }

    const uint8_t *getOutputTensorDataAddress(size_t index) const {
        assert(outputTensorPointers[index]);
        return outputTensorPointers[index];
    }

    pek::Shape getOutputTensorFinalShape(size_t index) const {
        return outputTensorFinalShapes[index];
    }

  private:
    static std::string deriveBinPath(const std::string &paramPath);
    static pek::Result<pek::Model> buildModelFromDescriptor(const pek::ModelDescriptor &desc,
                                                            const ncnn::Net &net);

    pek::Result<void> setTensorSizes();
    pek::Result<void> setScalarInputValues();
    pek::Result<ncnn::Mat> createInputMat(size_t tensorIndex) const;
    pek::Result<void> copyOutputMat(size_t tensorIndex, const ncnn::Mat &mat);
    pek::Result<void> applyTensorFeedback();

    ncnn::Net net;
    pek::ModelDescriptor modelDescriptor;
    pek::Model model;

    std::string paramPath;
    std::string binPath;
    bool setupReady = false;

    std::vector<std::vector<uint8_t>> inputTensors;
    std::vector<std::vector<uint8_t>> outputTensors;

    const uint8_t *outputTensorPointers[pek::MaxTensorCount] = {nullptr};
    pek::Shape outputTensorFinalShapes[pek::MaxTensorCount];
};

} // namespace pek::ncnnrt
