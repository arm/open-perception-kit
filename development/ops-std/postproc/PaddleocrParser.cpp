#include "postproc/PaddleocrParser.h"
#include "amp/Bitmap.h"
#include "amp/Perception.h"

#include <cmath>
#include <cstdint>

using namespace amp;

amp::Result<void> PaddleOcrDetectionParser::parse(const amp::TensorParser::Input &input,
                                                  amp::Perception::Layer &detectionResult) {

    assert(input.tensors[0]);

    assert(input.tensors[0]->getShape().dimensionCount == 4);
    assert(input.tensors[0]->getShape().valueCount[1] == 1);

    size_t maskHeight = input.tensors[0]->getShape().valueCount[2];
    size_t maskWidth = input.tensors[0]->getShape().valueCount[3];

    assert(maskHeight == input.inferenceInfo.image.modelHeight);
    assert(maskWidth == input.inferenceInfo.image.modelWidth);

    detectionResult.detections.push_back(Perception::SegmentationMap());

    auto &sm = std::get<Perception::SegmentationMap>(detectionResult.detections.back());
    sm.bitmap = amp::Bitmap(amp::Bitmap::Type::Uint8, maskWidth, maskHeight);

    uint8_t *dst = (uint8_t *)sm.bitmap.getData();

    float minLogit = std::numeric_limits<float>::infinity();
    float maxLogit = -std::numeric_limits<float>::infinity();

    for (size_t i = 0; i < maskWidth * maskHeight; i++) {
        float logit = input.tensors[0]->get(i);

        minLogit = std::min(minLogit, logit);
        maxLogit = std::max(maxLogit, logit);

        float p = 1.0f / (1.0f + std::exp(-logit)); // sigmoid
        int v = (int)std::lround(p * 255.0f);
        v = std::max(0, std::min(255, v));
        dst[i] = (uint8_t)v;
    }

    detectionResult.contentType = "ocrDetectionSegmentation";

    return {};
}
