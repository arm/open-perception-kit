#pragma once

#include "amp/PerceptionContext.h"
#include "amp/Result.h"
#include "amp/TensorParser.h"
#include "amp/TensorView.h"

namespace amp {

struct PaddleOcrDetectionParser : public amp::TensorParser {

    virtual amp::Result<void> parse(const amp::TensorParser::Input &input,
                                    amp::RawDetectionLayer &output) override;
};

} // namespace amp
