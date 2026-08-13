/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/DummyParser.h"
#include "Log.h"
#include "pek/TensorParser.h"
#include "pek/Types.h"

#include <cmath>
#include <string>

using namespace pek;
using namespace pek::stdop::postproc;

pek::Result<void> DummyParser::parse(const pek::TensorParser::Input &input,
                                     perception::FrameResults &results) {
    (void)results;

    bool log = input.attributes.getBoolOrDefault("log", false);

    if (log) {
        std::string log = "DummyParser got tensors: \n";

        for (size_t i = 0; i < pek::MaxTensorCount; i++) {
            if (input.tensors[i] == nullptr)
                continue;
            log += input.tensors[i]->getShape().toString() + "\n";
        }

        pek::log::info("DummyParser {}", log);
    }

    return {};
}
