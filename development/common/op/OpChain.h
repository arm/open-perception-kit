/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainDescriptor.h"
#include "op/OpRef.h"
#include "pek/Result.h"

#include <string_view>
#include <vector>

namespace pek::op {

/**
 * @brief Container and executor for a chain of operations.
 *
 * An OpChain manages a sequence of operations (Ops) that are executed in order, with optional
 * looping behavior for grouped operations. Operations communicate through a shared OpChainContext.
 * The chain is typically loaded from a descriptor (JSON) and can be executed repeatedly.
 */
class OpChain {
    std::string name;
    std::string displayName;
    std::string task;
    std::string runtime;
    std::vector<pek::op::OpRef> opRefs;
    std::vector<pek::op::Op *> opPtrs;

  public:
    /**
     * @brief Initializes the chain from an OpChainDescriptor.
     *
     * Loads operation definitions and creates Op instances from the descriptor.
     * Must be followed by bind() and then execute() can be called.
     *
     * @param descriptor Descriptor containing chain name and operation definitions.
     * @return Result indicating success or failure of setup.
     */
    pek::Result<void> setupFromDescriptor(const pek::op::OpChainDescriptor &descriptor);
    /**
     * @brief Initializes the chain by loading an OpChainDescriptor from a JSON file.
     *
     * Equivalent to loading JSON manually and calling setupFromDescriptor().
     *
     * @param jsonFile Path to JSON file containing OpChainDescriptor.
     * @return Result indicating success or failure of setup.
     */
    pek::Result<void> setupFromFile(const std::string &jsonFile);

    /**
     * @brief Gets the name of this chain.
     * @return Const reference to the chain name string.
     */
    const std::string &getName() const;
    const std::string &getDisplayName() const;
    const std::string &getTask() const;
    const std::string &getRuntime() const;
    std::vector<std::string_view> getProvidedContentTypes() const;
    std::vector<std::string_view> getRequiredContentTypes() const;
    /**
     * @brief Adds an operation reference to the chain.
     *
     * Typically used during manual construction before calling bind().
     *
     * @param opRef Reference to a dynamically-loaded operation.
     */
    void add(pek::op::OpRef &opRef);

    /**
     * @brief Binds all operations together and resolves inter-operation dependencies.
     *
     * Calls the bind() method on each operation, allowing them to discover and link
     * to other operations in the chain (e.g., an inference postprocessor finding the
     * upstream inference operation).
     *
     * Must be called after all operations are added, but before execute().
     *
     * @return Result indicating success or failure of binding.
     */
    pek::Result<void> bind();

    /**
     * @brief Executes the complete chain once.
     *
     * Processes all operations in sequence. If operations have loopId values, OpChain
     * uses OpSignal return values to continue, exit loop groups, or abort execution.
     *
     * @param opChainContext Mutable context passed to each operation's process() method.
     * @return Result indicating success or failure of execution.
     */
    pek::Result<void> execute(pek::op::OpChainContext &opChainContext);

    /**
     * @brief Generates a human-readable debug string representation of the chain.
     *
     * Shows each operation with its library name, operation name, and loop group (if any).
     *
     * @return String representation showing chain structure.
     */
    std::string toString() const {
        std::string ret;
        for (size_t i = 0; i < opPtrs.size(); i++) {
            auto op = opPtrs[i];
            if (op->loopId)
                ret += fmt::format("[{}]{}/{}\n", op->loopId, op->libName, op->opName);
            else
                ret += fmt::format("{}/{}\n", op->libName, op->opName);
        }
        return ret;
    }
};

} // namespace pek::op
