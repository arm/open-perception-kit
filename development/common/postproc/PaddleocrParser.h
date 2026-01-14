#pragma once

#include "amp/PerceptionContext.h"
#include "amp/Result.h"
#include "amp/TensorInOut.h"
#include "amp/TensorReader.h"

namespace amp {

struct PaddleOcrDetectionParser : public amp::NetworkOutputParser {

    virtual amp::Result<void> parse(const amp::TensorReader *tensorReades[4],
                                    const NetworkOutputParser::Settings &settings,
                                    const NetworkOutputParser::InferenceMetadata &metaData,
                                    amp::DetectionResult &detectionResult) override;
};

} // namespace amp
