#include "op/OpChain.h"

#include "amp/String.h"

#include "op/OpChainDescriptor.h"

using namespace amp;

amp::Result<void> OpChain::setupFromFile(const std::string &filePath) {
    auto descResult = amp::OpChainDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected{descResult.error()};
    }

    for (const auto &op : (*descResult).ops) {

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

        ops.push_back(std::move(opRef));

        printf("*");
    }

    return {};
}
