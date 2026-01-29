#pragma once

#include "amp/AttributeMap.h"
#include "op/Op.h"

#include <string>

namespace amp {

struct OpFactory {

    static Op *createOpInstance(const std::string &opId,
                                const AttributeMap &attributes = AttributeMap());
};

} // namespace amp
