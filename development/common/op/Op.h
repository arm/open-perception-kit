/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pek/AttributeMap.h"
#include "pek/Model.h"
#include "pek/Result.h"

#include "op/OpChainContext.h"

namespace pek::op {

/**
 * @brief Scheduler signal returned by an operation after process() completes.
 */
enum class OpSignal {
    Continue,  ///< Continue with the next operation in the chain.
    BreakLoop, ///< Skip the rest of the current loop group.
    AbortChain ///< Stop executing the current chain without reporting an error.
};

/**
 * @brief Interface for operations that provide tensor input/output information.
 *
 * Operations implementing this interface expose the inference model and tensor data
 * addresses for use by other operations in the chain.
 */
struct OpInterfaceInference {
    virtual ~OpInterfaceInference() = default;
    /**
     * @brief Retrieves the inference model associated with this operation.
     * @return Const reference to the Model object.
     */
    virtual const pek::Model &getModel() const = 0;
    /**
     * @brief Retrieves the memory address of a tensor by index.
     * @param index Tensor index in the model's output.
     * @return Pointer to tensor data at the specified index.
     */
    virtual uint8_t *getTensorDataAddress(size_t index) const = 0;
};

/**
 * @brief Interface for operations that perform inference postprocessing.
 *
 * Operations implementing this interface are responsible for parsing inference output
 * tensors and populating Perception objects with detected results.
 */
struct OpInterfacePostprocessor {
    virtual ~OpInterfacePostprocessor() = default;
    virtual std::vector<std::string_view> getProvidedContentTypes() const = 0;
};

/**
 * @brief Interface for operations that consume semantic FrameResults content.
 */
struct OpInterfaceContentConsumer {
    virtual ~OpInterfaceContentConsumer() = default;
    virtual std::vector<std::string_view> getRequiredContentTypes() const = 0;
};

/**
 * @brief Base interface for operations in an OpChain.
 *
 * An Op represents a processing unit that can be executed as part of a chain of operations.
 * Operations inherit from this interface and implement three lifecycle methods: configure,
 * bind, and process. Some operations may also implement optional interfaces like
 * OpInterfaceInference or OpInterfacePostprocessor for specialized interactions.
 */
struct Op {
    virtual ~Op() = default;

    /**
     * @brief Configures the operation with attributes from the OpChainDescriptor.
     *
     * Called during OpChain setup to initialize the operation with its configuration
     * parameters (e.g., model path, buffer names, inference settings).
     *
     * @param attributes AttributeMap containing operation-specific configuration.
     * @return Result indicating success or failure of configuration.
     */
    virtual Result<void> configure(const AttributeMap &attributes) = 0;

    /**
     * @brief Binds the operation to its position in the chain and to other operations.
     *
     * Called when the OpChain is built. Allows the operation to discover other operations
     * in the chain and establish references (e.g., finding an upstream inference op).
     *
     * @param index Position of this operation in the chain.
     * @param ops Vector of all operations in the chain; this op is at ops[index].
     * @return Result indicating success or failure of binding.
     */
    virtual Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) = 0;

    /**
     * @brief Executes the operation once per OpChain loop iteration.
     *
     * Performs the operation's work (e.g., preprocessing, inference, postprocessing).
     * May read/write context state including bitmaps, tensors, and Perception objects.
     * Scheduler control is returned explicitly as an OpSignal.
     *
     * @param opChainContext Mutable context for sharing state across operations.
     * @return Continue, loop-break, or abort signal, or an error on failure.
     */
    virtual Result<OpSignal> process(OpChainContext &opChainContext) = 0;

    [[nodiscard]] perception::metadata::ProducerInfoT
    producerInfo(std::string_view inferElementId,
                 std::string_view implementation,
                 std::string_view fallbackComponent) const;

    std::string libName;    ///< Name of the shared library providing this operation.
    std::string opName;     ///< Name of the operation class within the library.
    std::string instanceId; ///< Stable descriptor identity or deterministic fallback.
    size_t index = 0;       ///< Position of this operation in the OpChain.
    size_t loopId = 0;      ///< Loop group ID; ops with the same loopId execute in a loop.

    /**
     * @brief Safely casts this operation to a derived type.
     * @tparam T Derived type to cast to.
     * @return Pointer to the operation as type T, or nullptr if the cast is invalid.
     */
    template <class T> T *as() noexcept {
        return dynamic_cast<T *>(this);
    }

    /**
     * @brief Safely casts this operation to a derived type (const variant).
     * @tparam T Derived type to cast to.
     * @return Const pointer to the operation as type T, or nullptr if the cast is invalid.
     */
    template <class T> const T *as() const noexcept {
        return dynamic_cast<const T *>(this);
    }
};

} // namespace pek::op
