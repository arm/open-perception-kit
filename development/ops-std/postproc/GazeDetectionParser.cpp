

#include "postproc/GazeDetectionParser.h"
#include "amp/Perception.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace amp;

inline float logitsToAngleDeg(const amp::TensorView *logits, float gazeRangeDeg = 90.0f) {
    const size_t n = logits->getCount();

    float maxLogit = logits->get(0);
    for (size_t i = 1; i < n; ++i)
        maxLogit = std::max(maxLogit, logits->get(i));

    float sum = 0.0f;
    float expected = 0.0f;

    for (size_t i = 0; i < n; ++i)
        sum += std::exp(logits->get(i) - maxLogit);

    for (size_t i = 0; i < n; ++i) {
        float p = std::exp(logits->get(i) - maxLogit) / sum;
        expected += static_cast<float>(i) * p;
    }

    const float step = (2.0f * gazeRangeDeg) / float(n - 1);
    return expected * step - gazeRangeDeg;
}

amp::Result<void> GazeDetectionParser::parse(const amp::TensorParser::Input &input,
                                             amp::Perception::Layer &detectionResult) {

    assert(input.tensors[0]);
    assert(input.tensors[1]);

    assert(input.tensors[0]->getShape().dimensionCount == 2);
    assert(input.tensors[1]->getShape().dimensionCount == 2);

    assert(input.tensors[0]->getShape().valueCount[0] == 1);
    assert(input.tensors[0]->getShape().valueCount[1] == 90);
    assert(input.tensors[1]->getShape().valueCount[0] == 1);
    assert(input.tensors[1]->getShape().valueCount[1] == 90);

    float yaw = logitsToAngleDeg(input.tensors[0]);
    float pitch = logitsToAngleDeg(input.tensors[1]);

    // yaw -= 18;
    // pitch += 6;

    // HACK remove if Perception replaces PerceptionContext
    amp::TensorParser::Input &ncInput = const_cast<amp::TensorParser::Input &>(input);
    Perception::YawPitch yp;
    yp.yaw = yaw;
    yp.pitch = pitch;
    yp.parentUuid = input.inferenceInfo.parentUuid;
    ncInput.perceptionLayer.detections.push_back(yp);
    // HACK

    static float minYaw = std::numeric_limits<float>::max();
    static float minPitch = std::numeric_limits<float>::max();
    static float maxYaw = std::numeric_limits<float>::min();
    static float maxPitch = std::numeric_limits<float>::min();

    if (yaw < minYaw)
        minYaw = yaw;
    if (yaw > maxYaw)
        maxYaw = yaw;
    if (pitch < minPitch)
        minPitch = pitch;
    if (pitch > maxPitch)
        maxPitch = pitch;

    // fmt::print("minYP {} {} maxYP {} {}\n", minYaw, minPitch, maxYaw, maxPitch);

    detectionResult.contentType = "eye-yp";
    Perception::YawPitch result;
    result.yaw = yaw;
    result.pitch = pitch;
    detectionResult.detections.push_back(result);

    return {};
}

/*
#include "postproc/GazeDetectionParser.h"
#include "amp/PerceptionContext.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace amp;

namespace {

inline float logitsToAngleDeg(const amp::TensorView* logits, float& expected, float gazeRangeDeg
= 90.0f) { const size_t n = logits->getCount(); assert(n > 1);

    float maxLogit = logits->get(0);
    for (size_t i = 1; i < n; ++i)
        maxLogit = std::max(maxLogit, logits->get(i));

    float denom = 0.0f;
    for (size_t i = 0; i < n; ++i)
        denom += std::exp(logits->get(i) - maxLogit);

    expected = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float p = std::exp(logits->get(i) - maxLogit) / denom;
        expected += static_cast<float>(i) * p;
    }

    const float step = (2.0f * gazeRangeDeg) / static_cast<float>(n - 1);
    return expected * step - gazeRangeDeg;
}

}

amp::Result<void> GazeDetectionParser::parse(const amp::TensorParser::Input& input,
                                             amp::RawDetectionLayer& ) {

    assert(input.tensors[0] && input.tensors[1]);

    // Expect [1, 90]
    const auto& s0 = input.tensors[0]->getShape();
    const auto& s1 = input.tensors[1]->getShape();
    assert(s0.dimensionCount == 2 && s1.dimensionCount == 2);
    assert(s0.valueCount[0] == 1 && s0.valueCount[1] == 90);
    assert(s1.valueCount[0] == 1 && s1.valueCount[1] == 90);

float expectedYaw = 0.0f;
float expectedPitch = 0.0f;

float yaw   = logitsToAngleDeg(input.tensors[0], expectedYaw, 90.0f);
float pitch = logitsToAngleDeg(input.tensors[1], expectedPitch, 90.0f);

//fmt::print("yaw expected_bin={} yaw_deg={}\n", expectedYaw, yaw);
//fmt::print("pitch expected_bin={} pitch_deg={}\n", expectedPitch, pitch);


    // HACK: remove once Perception replaces PerceptionContext (avoid const_cast later)
    auto& ncInput = const_cast<amp::TensorParser::Input&>(input);
    Perception::YawPitch yp;
    yp.yaw = yaw;
    yp.pitch = pitch;
    yp.parentUuid = input.inferenceInfo.parentUuid;
    ncInput.perceptionLayer.detections.push_back(yp);
    // HACK

    static float minYaw   =  std::numeric_limits<float>::infinity();
    static float minPitch =  std::numeric_limits<float>::infinity();
    static float maxYaw   = -std::numeric_limits<float>::infinity();
    static float maxPitch = -std::numeric_limits<float>::infinity();

    minYaw   = std::min(minYaw, yaw);
    maxYaw   = std::max(maxYaw, yaw);
    minPitch = std::min(minPitch, pitch);
    maxPitch = std::max(maxPitch, pitch);

//fmt::print("YP yaw={} pitch={} | minYP {} {} maxYP {} {}\n",
  //             yaw, pitch, minYaw, minPitch, maxYaw, maxPitch);

    return {};
}

*/
