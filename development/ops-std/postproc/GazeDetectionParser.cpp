#include "postproc/GazeDetectionParser.h"
#include "amp/PerceptionContext.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace amp;

inline float logitsToAngleDeg(amp::TensorView *logits) {

    float maxLogit = logits->get(0);
    for (size_t i = 1; i < logits->getCount(); i++) {
        maxLogit = std::max(maxLogit, logits->get(i));
    }

    float sum = 0.0f;
    std::vector<float> probs;
    probs.resize(logits->getByteCount());

    for (size_t i = 0; i < logits->getCount(); i++) {
        probs[i] = std::exp(logits->get(i) - maxLogit);
        sum += probs[i];
    }

    for (size_t i = 0; i < logits->getCount(); i++) {
        probs[i] /= sum;
    }

    float expectedBin = 0.0f;
    for (size_t i = 0; i < logits->getCount(); ++i) {
        expectedBin += static_cast<float>(i) * probs[i];
    }

    float angleDeg = expectedBin * 2.0f - 90.0f;

    return angleDeg;
}

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

    float yaw = logitsToAngleDeg(input.tensors[0]);
    float pitch = logitsToAngleDeg(input.tensors[1]);

    fmt::print("gaze {} {}\n", yaw, pitch);

    return {};
}
