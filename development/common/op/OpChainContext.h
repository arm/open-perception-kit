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

// all the data generated in the OpChain of an element
// this data is thrown away when the opchain is finished
// for generate permanent data, the Op must copy it to the PerceptionContext
// buffers stored here as pointers must be valid through the execution of the chain
struct OpChainContext {

    bool execute = true;

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

    bool inferenceControllerExecuted = false;
};

} // namespace amp
