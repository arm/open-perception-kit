#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "tensor_types.h"
#include "tensor_view.h"
#include "output_types.h"

namespace uflw {

    class Tensor {    
    
    public:

        

    private:

        ValuePointer data = nullptr;
        size_t size = 0;

    };

}

