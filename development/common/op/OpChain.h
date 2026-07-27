/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainDescriptor.h"
#include "op/OpRef.h"
#include "pek/Result.h"

#include <stop_token>
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
    std::vector<pek::op::OpRef> opRefs;
    std::vector<pek::op::Op *> opPtrs;

    /**
     * @brief Validates that operations with the same loopId are contiguous.
     * @return Result indicating success or failure of validation.
     */
    pek::Result<void> validateGroupedLoopIds();
    /**
     * @brief Validates that all loop groups have consistent sizes.
     * @return Result indicating success or failure of validation.
     */
    pek::Result<void> validateLoopGroupSizes();
    /**
     * @brief Performs full chain validation including group IDs and loop sizes.
     * @return Result indicating success or failure of validation.
     */
    pek::Result<void> validate();

  public:
    /**
     * @brief Initializes the chain from an OpChainDescriptor.
     *
     * Loads operation definitions, creates Op instances, and binds the completed
     * chain before returning. execute() can be called after successful setup.
     *
     * Operations receive shared setup controls while they configure. Model loading
     * remains synchronous within this call; callers may choose another execution
     * thread and provide cooperative cancellation.
     *
     * @param descriptor Descriptor containing chain name and operation definitions.
     * @param stopToken Optional cooperative cancellation token.
     * @return Result indicating success or failure of setup.
     */
    pek::Result<void> setupFromDescriptor(const pek::op::OpChainDescriptor &descriptor,
                                          std::stop_token stopToken = {});
    /**
     * @brief Initializes the chain by loading an OpChainDescriptor from a JSON file.
     *
     * Equivalent to loading JSON manually and calling setupFromDescriptor().
     *
     * @param jsonFile Path to JSON file containing OpChainDescriptor.
     * @param stopToken Optional cooperative cancellation token.
     * @return Result indicating success or failure of setup.
     */
    pek::Result<void> setupFromFile(const std::string &jsonFile, std::stop_token stopToken = {});

    /**
     * @brief Gets the name of this chain.
     * @return Const reference to the chain name string.
     */
    const std::string &getName();
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
