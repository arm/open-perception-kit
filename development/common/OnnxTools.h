#pragma once

#include <onnxruntime_cxx_api.h>

#include "uniflow/public_types.h"
#include "uniflow/uniflow.h"

struct OnnxOutputTensor {

    //OnnxOutputTensor()    

};

class OnnxTools {

public:

    static size_t getOnnxValueTypeByteSize(ONNXTensorElementDataType tensorType);

};