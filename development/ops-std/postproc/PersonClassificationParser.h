#pragma once

#include "amp/Perception.h"
#include "amp/Result.h"
#include "amp/TensorParser.h"
#include "amp/TensorView.h"

namespace amp {

struct PersonClassificationParser : public amp::TensorParser {

    virtual amp::Result<void> parse(const amp::TensorParser::Input &input,
                                    amp::Perception::Layer &output) override;
};

} // namespace amp
