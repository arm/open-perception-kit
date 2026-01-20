#pragma once

#include "amp/Result.h"
#include "op/Op.h"

#include <vector>

namespace amp {

struct OpChain {
    amp::Result<void> setupFromFile(const std::string &jsonFile);

    std::vector<amp::OpRef> ops;
};

} // namespace amp