#pragma once

#include "tensor_types.h"

namespace uf {

    struct Box { float x, y, w, h; };
    struct ProbabilityClass { float probabity = -1.0f; int labelIndex = -1; };
    struct ProbabilityClassBox : public Box, public ProbabilityClass {}; 

}