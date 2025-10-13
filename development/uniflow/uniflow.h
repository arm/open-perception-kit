#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "tensor_types.h"
#include "tensor_view.h"
#include "output_types.h"

namespace uf {

    struct ValueLayout {

        struct Position {
            size_t offset;
        };

        size_t fullStepByteStride = -1;
        

    };

}

