/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/BitmapView.h"
#include "pek/Perception.h"
#include "pek/TensorView.h"
#include "pek/Types.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace pek::op {

struct OpChainContext {

    // controls the loop execution, if loopGroup is not empty
    // the system will loop back to the loop head Op
    size_t loopId = 0;
    bool breakLoop = false;
    bool abort = false;

    // named bitmap views that ops can read/write, e.g. to share the video frame across multiple ops
    std::map<std::string, pek::BitmapView> bitmapViews;

    pek::BitmapView *getBitmapView(const std::string &name) {
        auto it = bitmapViews.find(name);
        if (it == bitmapViews.end()) {
            return nullptr;
        }
        return &it->second;
    }

    // logical image tensor crops, inference loop consumes them, when ready inference loop ends
    std::vector<pek::PixelRect> inferenceImageCrops;
    std::vector<uint64_t> inferenceImageCropUuids;

    // info about the last executed inference
    uint64_t inferenceSourceUuid = 0;
    size_t inferenceOutputTensorCount = 0;
    pek::TensorView inferenceOutputTensors[pek::MaxTensorCount];
    pek::InferenceInfo inferenceInfo;

    bool hasRootLayer = false;
    Perception::Layer rootLayer;

    // the perception object that ops can read/write to produce the final perception result
    Perception *perception = nullptr;
};

} // namespace pek::op
