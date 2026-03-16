/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/BitmapView.h"
#include "amp/Perception.h"
#include "amp/TensorView.h"
#include "amp/Types.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace amp {

struct OpChainContext {

    // controls the loop execution, if loopGroup is not empty
    // the system will loop back to the loop head Op
    size_t loopId = 0;
    bool breakLoop = false;

    // named bitmap views that ops can read/write, e.g. to share the video frame across multiple ops
    std::map<std::string, amp::BitmapView> bitmapViews;

    amp::BitmapView *getBitmapView(const std::string &name) {
        auto it = bitmapViews.find(name);
        if (it == bitmapViews.end()) {
            return nullptr;
        }
        return &it->second;
    }

    // logical image tensor crops, inference loop consumes them, when ready inference loop ends
    std::vector<amp::PixelRect> inferenceImageCrops;
    std::vector<uint64_t> inferenceImageCropUuids;

    // info about the last executed inference
    uint64_t inferenceSourceUuid = 0;
    size_t inferenceOutputTensorCount = 0;
    amp::TensorView inferenceOutputTensors[amp::MaxTensorCount];
    amp::InferenceInfo inferenceInfo;

    Perception::Layer rootLayer;

    // the perception object that ops can read/write to produce the final perception result
    Perception *perception = nullptr;
};

} // namespace amp
