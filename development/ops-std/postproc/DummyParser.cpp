/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/DummyParser.h"
#include "amp/Perception.h"
#include "amp/TensorParser.h"
#include "amp/Types.h"

#include <cmath>
#include <fmt/core.h>
#include <string>

using namespace amp;

amp::Result<void> DummyParser::parse(const amp::TensorParser::Input &input,
                                     amp::Perception::Layer &detectionResult) {

    bool log = input.attributes.getBoolOrDefault("log", false);

    if (log) {
        std::string log = "DummyParser got tensors: \n";

        for (size_t i = 0; i < amp::MaxTensorCount; i++) {
            if (input.tensors[i] == nullptr)
                continue;
            log += input.tensors[i]->getShape().toString() + "\n";
        }

        fmt::print("DummyParser {}", log);
    }

    return {};
}