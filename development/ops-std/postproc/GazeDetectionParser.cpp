#include "postproc/GazeDetectionParser.h"
#include "amp/PerceptionContext.h"

#include <cmath>
#include <cstdint>

using namespace amp;

amp::Result<void> GazeDetectionParser::parse(const amp::TensorParser::Input &input,
                                             amp::RawDetectionLayer &detectionResult) {

    assert(input.tensors[0]);
    assert(input.tensors[1]);

    assert(input.tensors[0]->getShape().dimensionCount == 2);
    assert(input.tensors[1]->getShape().dimensionCount == 2);

    assert(input.tensors[0]->getShape().valueCount[0] == 1);
    assert(input.tensors[1]->getShape().valueCount[0] == 1);
    assert(input.tensors[0]->getShape().valueCount[1] == 90);
    assert(input.tensors[1]->getShape().valueCount[1] == 90);

    return {};
}
