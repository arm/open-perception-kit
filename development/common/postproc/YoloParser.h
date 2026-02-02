#pragma once

#include "amp/PerceptionContext.h"
#include "amp/Result.h"
#include "amp/TensorView.h"
#include "postproc/TensorParser.h"

//
namespace amp {

struct YoloLikeParser : public amp::TensorParser {

    virtual amp::Result<void> parse(const amp::TensorParser::Input &input,
                                    amp::RawDetectionLayer &output) override;
};

} // namespace amp
