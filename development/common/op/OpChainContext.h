#pragma once

#include "amp/BitmapView.h"
#include "amp/Perception.h"
#include "amp/Tags.h"
#include "amp/TensorView.h"
#include "amp/Types.h"
#include <cstdint>
#include <map>
#include <vector>

namespace amp {

struct OpChainContext {

    bool execute = true;

    /*int inLoopStartIndex = -1, inLoopEndIndex = -1;
    void exitLoop() {
        assert(inLoopStartIndex != -1);
        assert(inLoopEndIndex != -1);
    }*/

    std::map<std::string, amp::BitmapView> bitmapViews;

    amp::BitmapView *getBitmapView(const std::string &name) {
        auto it = bitmapViews.find(name);
        if (it == bitmapViews.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::vector<amp::PixelRect> inferenceCrops;
    std::vector<uint64_t> inferenceCropUuids;

    // info about the last executed inference
    uint64_t inferenceSourceUuid = 0;
    size_t inferenceOutputTensorCount = 0;
    amp::TensorView inferenceOutputTensors[amp::MaxTensorCount];
    amp::InferenceInfo inferenceInfo;

    Perception *perception = nullptr;

    int loopStartOpIndex = -1;

    bool inferenceControllerExecuted = false;
};

} // namespace amp
