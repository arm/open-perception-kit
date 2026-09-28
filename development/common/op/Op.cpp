/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/Op.h"

#include <fmt/format.h>

using namespace opk::op;

open_perception_kit::metadata::ProducerInfoT
Op::producerInfo(std::string_view inferElementId,
                 std::string_view implementation,
                 std::string_view fallbackComponent) const {
    open_perception_kit::metadata::ProducerInfoT result;
    result.instance_id = fmt::format("{}/{}", inferElementId, instanceId);
    result.component = libName.empty() || opName.empty() ? fallbackComponent
                                                         : fmt::format("{}/{}", libName, opName);
    result.implementation = implementation;
    return result;
}
