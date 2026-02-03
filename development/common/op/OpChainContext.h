#pragma once

#include "amp/BitmapView.h"
#include "amp/PerceptionContext.h"
#include "amp/Tags.h"
#include "amp/TensorView.h"
#include "amp/Types.h"
#include <cstdint>
#include <map>

namespace amp {

// all the data generated in the OpChain of an element
// this data is thrown away when the opchain is finished
// for generate permanent data, the Op must copy it to the PerceptionContext
// buffers stored here as pointers must be valid through the execution of the chain
struct OpChainContext {

    struct TaggedBitmapView {
        amp::Tags tags;
        amp::BitmapView bitmapView;
    };

    std::map<std::string, amp::BitmapView> bitmapViews;

    amp::BitmapView *getBitmapView(const std::string &name) {
        auto it = bitmapViews.find(name);
        if (it == bitmapViews.end()) {
            return nullptr;
        }
        return &it->second;
    }

    // info about the last executed inference
    size_t inferenceOutputTensorCount = 0;
    amp::TensorView inferenceOutputTensors[amp::MaxTensorCount];
    amp::InferenceInfo inferenceInfo;

    PerceptionContext *perceptionContext = nullptr;
};

} // namespace amp
