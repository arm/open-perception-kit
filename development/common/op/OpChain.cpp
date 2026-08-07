/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/OpChain.h"

#include "Log.h"
#include "Validator.h"
#include "pek/String.h"
#include "tools.h"

#include "op/Op.h"
#include "op/OpChainDescriptor.h"
#include "perf/PerformanceMetrics.h"

#include <string>
using namespace pek::op;

const std::string &OpChain::getName() {
    return this->name;
}

pek::Result<void> OpChain::setupFromDescriptor(const pek::op::OpChainDescriptor &descriptor) {
    if (const auto validation = pek::config::validateOpChainSemantics(descriptor); !validation.ok())
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InvalidOpChain, validation.toText()));

    name = descriptor.name;

    for (const auto &op : descriptor.ops) {

        if (pek::utf8::count(op.id, '/') != 1) {
            return tl::unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("Op id must be library/op, but found: [{}]", op.id)));
        }

        std::string libName = pek::utf8::split(op.id, "/")[0];
        std::string opName = pek::utf8::split(op.id, "/")[1];

        pek::op::OpRef opRef;
        auto bindResult = opRef.bind(libName, opName);
        if (!bindResult) {
            return bindResult;
        }

        opRef->libName = libName;
        opRef->opName = opName;

        opRef->loopId = op.loopId.value_or(0);

        auto configureResult = opRef->configure(op.attributes);
        if (!configureResult) {
            return configureResult;
        }

        add(opRef);
    }

    auto chainBindResult = bind();
    if (!chainBindResult) {
        return tl::unexpected(std::move(chainBindResult.error()));
    }

    pek::log::info("{}", pek::log::tools::enframe(this->toString(), "OpChain"));
    pek::log::notice("OpChain is valid\n");

    return {};
}

pek::Result<void> OpChain::setupFromFile(const std::string &filePath) {
    pek::log::info("Loading OpChain from file: [{}]\n", filePath);
    auto descResult = pek::op::OpChainDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected(std::move(descResult.error()));
    }

    return setupFromDescriptor(*descResult);
}

void OpChain::add(pek::op::OpRef &opRef) {
    opRefs.push_back(std::move(opRef));
}

pek::Result<void> OpChain::bind() {
    opPtrs.resize(opRefs.size());
    for (size_t i = 0; i < opRefs.size(); i++) {
        opPtrs[i] = opRefs[i].get();
    }

    for (size_t i = 0; i < opPtrs.size(); i++) {
        auto opBindResult = opPtrs[i]->bind(i, opPtrs);
        if (!opBindResult) {
            return opBindResult;
        }
    }

    return {};
}

pek::Result<void> OpChain::execute(pek::op::OpChainContext &opChainContext) {
    const auto metricName = name.empty() ? std::string("opchain") : fmt::format("opchain/{}", name);
    PEK_PERF_SCOPE(metricName);

    size_t currentIndex = 0;

    auto firstWorkerIndexForLoop = [this](size_t index, size_t loopId) {
        size_t firstGroupIndex = index;
        while (firstGroupIndex > 0 && opPtrs[firstGroupIndex - 1]->loopId == loopId) {
            --firstGroupIndex;
        }

        // Skip the controller op. Loop groups are validated to have at least two ops.
        return firstGroupIndex + 1;
    };

    auto nextIndexAfterLoop = [this](size_t index, size_t loopId) {
        while (index < opPtrs.size() && opPtrs[index]->loopId == loopId) {
            ++index;
        }
        return index;
    };

    while (currentIndex < opPtrs.size()) {
        Op *op = opPtrs[currentIndex];
        const size_t loopId = op->loopId;

        auto result = op->process(opChainContext);
        if (!result) {
            return tl::unexpected(std::move(result.error()));
        }

        switch (*result) {
        case OpSignal::AbortChain:
            return {};
        case OpSignal::BreakLoop:
            currentIndex =
                (loopId == 0) ? currentIndex + 1 : nextIndexAfterLoop(currentIndex, loopId);
            continue;
        case OpSignal::Continue:
            break;
        }

        const bool lastInChain = currentIndex + 1 == opPtrs.size();
        const size_t nextLoopId = lastInChain ? 0 : opPtrs[currentIndex + 1]->loopId;

        if (loopId != 0 && (lastInChain || nextLoopId != loopId)) {
            currentIndex = firstWorkerIndexForLoop(currentIndex, loopId);
        } else {
            ++currentIndex;
        }
    }

    return {};
}
