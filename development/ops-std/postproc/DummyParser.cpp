/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/DummyParser.h"
#include "pek/Perception.h"
#include "pek/TensorParser.h"
#include "pek/Types.h"

#include <cmath>
#include <fmt/core.h>
#include <string>

using namespace pek;
using namespace pek::stdop::postproc;

Result<void> DummyParser::parse(const pek::TensorParser::Input &input,
                                pek::Perception::Layer &detectionResult) {

    bool log = input.attributes.getBoolOrDefault("log", false);

    if (log) {
        std::string log = "DummyParser got tensors: \n";

        for (size_t i = 0; i < pek::MaxTensorCount; i++) {
            if (input.tensors[i] == nullptr)
                continue;
            log += input.tensors[i]->getShape().toString() + "\n";
        }

        fmt::print("DummyParser {}", log);
    }

    return {};
}