#pragma once

#include "amp/PerceptionContext.h"
#include <cstdint>

namespace amp {

// all the data generated in the OpChain of an element
// no data is owned here
// for generate permanent data, the Op must copy it to the PerceptionContext
struct OpContext {

    struct InputImageData {
        uint8_t *data = nullptr;
        size_t width = 0, height = 0, stride = 0;
    };

    InputImageData inputImages[4];
    PerceptionContext *perceptionContext = nullptr;
};

} // namespace amp