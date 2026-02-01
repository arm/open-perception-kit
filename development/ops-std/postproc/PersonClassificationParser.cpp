#include "postproc/PersonClassificationParser.h"
#include "amp/PerceptionContext.h"

#include <cmath>
#include <cstdint>

using namespace amp;

amp::Result<void> PersonClassificationParser::parse(const amp::TensorParser::Input &input,
                                                    amp::RawDetectionLayer &detectionResult) {

    assert(input.tensors[0]);

    assert(input.tensors[0]->getShape().dimensionCount == 2);

    assert(input.tensors[0]->getShape().valueCount[0] == 1);
    assert(input.tensors[0]->getShape().valueCount[1] == 2);

    // printf("%f %f\n", input.tensors[0]->get(0), input.tensors[0]->get(1));

    return {};
}
