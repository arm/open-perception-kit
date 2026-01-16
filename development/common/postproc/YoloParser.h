#pragma once

#include "amp/PerceptionContext.h"
#include "amp/Result.h"
#include "amp/TensorReader.h"
#include "postproc/TensorParser.h"

//
namespace amp {

struct YoloLikeParser : public amp::TensorParser {

    virtual amp::Result<void> parse(const amp::TensorParser::Input &input,
                                    amp::DetectionResult &detectionResult) override;
};

} // namespace amp
