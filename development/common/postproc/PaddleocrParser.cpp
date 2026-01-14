#include "postproc/PaddleOcrParser.h"

#include <cmath>
#include <cstdint>

using namespace amp;

amp::Result<void>
PaddleOcrDetectionParser::parse(const amp::TensorReader *tensorReaders[4],
                                const NetworkOutputParser::Settings &settings,
                                const NetworkOutputParser::InferenceMetadata &metaData,
                                amp::DetectionResult &detectionResult) {

    assert(tensorReaders[0]);

    assert(tensorReaders[0]->getShape().dimensionCount == 4);
    assert(tensorReaders[0]->getShape().valueCount[1] == 1);

    size_t maskHeight = tensorReaders[0]->getShape().valueCount[2];
    size_t maskWidth = tensorReaders[0]->getShape().valueCount[3];

    assert(maskHeight == metaData.image.modelHeight);
    assert(maskWidth == metaData.image.modelWidth);

    detectionResult.maps.push_back(amp::Map8());
    detectionResult.maps.back().map.resize(maskWidth * maskHeight);

    uint8_t *dst = detectionResult.maps.back().map.data();

    float *fp = (float *)tensorReaders[0]->getData();
    for (size_t i = 0; i < maskHeight * maskWidth; i++)
        //    if((uint8_t)fp[i])
        //      printf("%u ", (uint8_t)fp[i]);

        float minProb = 1000000.0;
    float maxProb = -1000000.0;

    float minLogit = 1e30f, maxLogit = -1e30f;

    for (size_t i = 0; i < maskWidth * maskHeight; i++) {
        float logit = tensorReaders[0]->get(i);

        minLogit = std::min(minLogit, logit);
        maxLogit = std::max(maxLogit, logit);

        float p = 1.0f / (1.0f + std::exp(-logit)); // sigmoid
        int v = (int)std::lround(p * 255.0f);
        v = std::max(0, std::min(255, v));
        dst[i] = (uint8_t)v;
    }
    // printf("logit min=%f max=%f\n", minLogit, maxLogit);

    detectionResult.maps.back().width = maskWidth;
    detectionResult.maps.back().height = maskHeight;

    return {};
}
