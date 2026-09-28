/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "postproc/DummyParser.h"
#include "Log.h"
#include "opk/TensorParser.h"
#include "opk/Types.h"

#include <cmath>
#include <string>

using namespace opk;
using namespace opk::stdop::postproc;

opk::Result<void> DummyParser::parse(const opk::TensorParser::Input &input,
                                     open_perception_kit::FrameResults &results) {
    (void)results;

    bool log = input.attributes.getBoolOrDefault("log", false);

    if (log) {
        std::string log = "DummyParser got tensors: \n";

        for (size_t i = 0; i < opk::MaxTensorCount; i++) {
            if (input.tensors[i] == nullptr)
                continue;
            log += input.tensors[i]->getShape().toString() + "\n";
        }

        opk::log::info("DummyParser {}", log);
    }

    return {};
}
