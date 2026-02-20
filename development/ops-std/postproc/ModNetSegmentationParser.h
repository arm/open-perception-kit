#pragma once

#include "amp/TensorParser.h"

namespace amp {

class ModNetSegmentationParser : public TensorParser {
  public:
    amp::Result<void> parse(const Input &input, Perception::Layer &layer) override;
};

} // namespace amp
