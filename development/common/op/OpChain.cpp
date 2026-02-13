#include "op/OpChain.h"

#include "amp/String.h"

#include "op/Op.h"
#include "op/OpChainDescriptor.h"

using namespace amp;

amp::Result<void> OpChain::setupFromDescriptor(const amp::OpChainDescriptor &descriptor) {
    for (const auto &op : descriptor.ops) {

        if (amp::utf8::count(op.id, '/') != 1) {
            return tl::unexpected(
                AMP_ERROR(amp::ErrorFlag::InvalidData,
                          fmt::format("Op id must be library/op, but found: [{}]", op.id)));
        }

        std::string libName = amp::utf8::split(op.id, "/")[0];
        std::string opName = amp::utf8::split(op.id, "/")[1];

        amp::OpRef opRef;
        auto bindResult = opRef.bind(libName, opName);
        if (!bindResult) {
            return bindResult;
        }

        auto configureResult = opRef->configure(op.attributes);
        if (!configureResult) {
            return configureResult;
        }

        add(opRef);
    }

    auto chainBindResult = bind();
    if (!chainBindResult) {
        return chainBindResult;
    }

    return {};
}

amp::Result<void> OpChain::setupFromFile(const std::string &filePath) {
    auto descResult = amp::OpChainDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected{descResult.error()};
    }
    return setupFromDescriptor(*descResult);
}

void OpChain::add(amp::OpRef &opRef) {
    opRefs.push_back(std::move(opRef));
}

amp::Result<void> OpChain::bind() {
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

amp::Result<void> OpChain::execute(amp::OpChainContext &opChainContext) {
    while (opChainContext.execute) {
        for (const auto &op : opPtrs) {
            if (opChainContext.execute == false)
                break;
            auto opResult = op->process(opChainContext);
            if (!opResult) {
                return opResult;
            }
        }
    }
    return {};
}