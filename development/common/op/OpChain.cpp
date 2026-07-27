/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/OpChain.h"

#include "Log.h"
#include "pek/String.h"
#include "tools.h"

#include "op/Op.h"
#include "op/OpChainDescriptor.h"
#include "op/OpSetupContext.h"

#include <unordered_map>
#include <unordered_set>

using namespace pek::op;

namespace {

pek::Result<void> modelLoadCancelled() {
    return tl::unexpected{
        PEK_ERROR(pek::ErrorFlag::SystemFailure, "OpChain model loading was cancelled")};
}

} // namespace

const std::string &OpChain::getName() {
    return this->name;
}

pek::Result<void> OpChain::setupFromDescriptor(const pek::op::OpChainDescriptor &descriptor,
                                               std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return modelLoadCancelled();

    name = descriptor.name;
    pek::op::OpSetupContext setupContext(stopToken);

    for (const auto &op : descriptor.ops) {
        if (stopToken.stop_requested())
            return modelLoadCancelled();

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

        opRef->group = op.group;
        opRef->loopId = op.loopId;

        auto configureResult = opRef->configure(op.attributes, setupContext);
        if (!configureResult) {
            return configureResult;
        }

        if (stopToken.stop_requested())
            return modelLoadCancelled();

        add(opRef);
    }

    if (stopToken.stop_requested())
        return modelLoadCancelled();

    auto chainBindResult = bind();
    if (!chainBindResult) {
        return tl::unexpected(std::move(chainBindResult.error()));
    }

    if (stopToken.stop_requested())
        return modelLoadCancelled();

    pek::log::info("{}", pek::log::tools::enframe(this->toString(), "OpChain"));

    // validation
    auto validateResult = validate();
    if (!validateResult) {
        return tl::unexpected(std::move(validateResult.error()));
    }

    if (stopToken.stop_requested())
        return modelLoadCancelled();

    pek::log::notice("OpChain is valid\n");

    return {};
}

pek::Result<void> OpChain::validateGroupedLoopIds() {
    std::unordered_set<size_t> closed;

    bool havePrev = false;
    size_t prevId{};

    for (size_t i = 0; i < opPtrs.size(); ++i) {
        Op *p = opPtrs[i];
        const size_t id = p->loopId;

        if (!havePrev) {
            havePrev = true;
            prevId = id;
            continue;
        }

        if (id == prevId) {
            continue; // still in same run
        }

        // we are leaving prevId's run
        if (prevId != 0) { // 0 is the non-group id
            closed.insert(prevId);
        }

        // if id is non-zero and already closed, it's invalid
        if (id != 0 && closed.contains(id)) {
            return tl::make_unexpected(PEK_ERROR(pek::ErrorFlag::InvalidOpChain,
                                                 "OpChain loopId values must be grouped together"));
        }

        prevId = id;
    }

    return {};
}

pek::Result<void> OpChain::validateLoopGroupSizes() {
    std::unordered_map<size_t, size_t> groupOpCount;
    for (Op *p : opPtrs) {
        if (p->loopId != 0)
            groupOpCount[p->loopId]++;
    }
    for (const auto &[id, count] : groupOpCount) {
        if (count < 2)
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidOpChain,
                          fmt::format("Loop group {} must contain at least 2 ops (controller + "
                                      "one worker), but has {}",
                                      id,
                                      count)));
    }
    return {};
}

pek::Result<void> OpChain::validate() {
    auto validateLoopIdsResult = validateGroupedLoopIds();
    if (!validateLoopIdsResult) {
        return validateLoopIdsResult;
    }
    auto validateLoopGroupSizesResult = validateLoopGroupSizes();
    if (!validateLoopGroupSizesResult) {
        return validateLoopGroupSizesResult;
    }
    return {};
}

pek::Result<void> OpChain::setupFromFile(const std::string &filePath, std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return modelLoadCancelled();

    pek::log::info("Loading OpChain from file: [{}]\n", filePath);
    auto descResult = pek::op::OpChainDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected(std::move(descResult.error()));
    }

    return setupFromDescriptor(*descResult, stopToken);
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
