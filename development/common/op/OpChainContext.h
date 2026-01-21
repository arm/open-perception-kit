#pragma once

#include "amp/BitmapView.h"
#include "amp/PerceptionContext.h"
#include <cstdint>
#include <map>

namespace amp {

// all the data generated in the OpChain of an element
// this data is thrown away when the opchain is finished
// for generate permanent data, the Op must copy it to the PerceptionContext
struct OpChainContext {

    std::map<std::string, amp::BitmapView> bitmapViews;

    PerceptionContext *perceptionContext = nullptr;
};

} // namespace amp
