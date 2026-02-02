#pragma once

#include "amp/BitmapView.h"
#include "amp/PerceptionContext.h"
#include "amp/TensorView.h"
#include "postproc/TensorParser.h"
#include <cstdint>
#include <map>

namespace amp {

// all the data generated in the OpChain of an element
// this data is thrown away when the opchain is finished
// for generate permanent data, the Op must copy it to the PerceptionContext
// buffers stored here as pointers must be valid through the execution of the chain
struct OpChainContext {

    std::map<std::string, amp::BitmapView> bitmapViews;

    amp::BitmapView *getBitmapView(const std::string &name) {
        auto it = bitmapViews.find(name);
        if (it == bitmapViews.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::map<std::string, amp::TensorView> tensorViews;

    amp::TensorView *getTensorView(const std::string &name) {
        auto it = tensorViews.find(name);
        if (it == tensorViews.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::string modelFamily;
    amp::TensorParser::Input tensorParserInput;

    PerceptionContext *perceptionContext = nullptr;
};

} // namespace amp
