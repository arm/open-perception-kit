#pragma once

#include "public_types.h"

namespace uflw {

    // Helper, should be removed
    struct DetectionBox {
        float x1, y1, x2, y2;
        float score;
        int label;
    };

    // Training range is used to normalize values (e.g. RGB U8 values) before quantize them.
    // The input values are normalized into the "min-max" range when Range type is set to Exact.
    // They are normalized to 0-1 or -1-1 if set to Auto see updateRange() for details.
    void updateRange(QuantizationArgs& qantArgs, ValueType valueType);

}


