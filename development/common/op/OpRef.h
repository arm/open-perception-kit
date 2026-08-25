/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <string>

#include "pek/AttributeMap.h"
#include "pek/Result.h"
#include "pek/Tools.h"

#include "op/OpChainContext.h"

namespace pek::op {

struct Op;

/**
 * @brief RAII handle for dynamically-loaded Operation instances.
 *
 * OpRef manages the lifetime of an operation loaded from a shared library (.so/.dll).
 * It handles library loading via dlopen, function symbol resolution, and handle cleanup via
 * dlclose. OpRef is move-enabled but non-copyable to ensure single ownership of the loaded library.
 */
class OpRef {
  public:
    /**
     * @brief Constructs an empty OpRef (null operation handle).
     */
    OpRef();
    /**
     * @brief Destroys the operation and closes its library handle.
     *
     * If a library is loaded, calls the operation's destroy/delete function and dlclose().
     */
    ~OpRef();

    /**
     * @brief Binds an operation by loading its library and resolving creation functions.
     *
     * Loads the shared library named soName, resolves the creation and destruction function
     * symbols, and creates an instance of the operation. The created operation must be
     * deleted by calling reset() or the destructor.
     *
     * @param soName Name of the shared library to load (e.g., "libopNN" for libopNN.so).
     * @param opName Name of the operation class within the library.
     * @return Result indicating success or failure of binding/loading.
     */
    Result<void> bind(const std::string &soName, const std::string &opName);

    /**
     * @brief Deleted copy constructor (non-copyable).
     */
    OpRef(const OpRef &) = delete;
    /**
     * @brief Deleted copy assignment operator (non-copyable).
     */
    OpRef &operator=(const OpRef &) = delete;

    /**
     * @brief Move constructor (enabled).
     *
     * Transfers ownership of the loaded operation and library from other to this.
     */
    OpRef(OpRef &&other) noexcept;
    /**
     * @brief Move assignment operator (enabled).
     *
     * Transfers ownership of the loaded operation and library from other to this.
     */
    OpRef &operator=(OpRef &&other) noexcept;

    /**
     * @brief Retrieves the operation pointer.
     * @return Const pointer to the loaded Op, or nullptr if not bound.
     */
    pek::op::Op *get() const noexcept {
        return op;
    }
    /**
     * @brief Dereferences the operation pointer.
     * @return Reference to the loaded Op; undefined behavior if not bound.
     */
    pek::op::Op &operator*() const {
        return *op;
    }
    /**
     * @brief Arrow operator for accessing operation members.
     * @return Pointer to the loaded Op; undefined behavior if not bound.
     */
    pek::op::Op *operator->() const noexcept {
        return op;
    }

  private:
    /**
     * @brief Function type for creating operation instances.
     *
     * Signature that all operation factories must follow. They take a config string
     * and return a void pointer (cast to Op*).
     */
    using CreateFn = void *(*)(const char *);
    /**
     * @brief Function type for destroying operation instances.
     */
    using DeleteFn = void (*)(void *);

    /**
     * @brief Releases the operation and closes its library handle.
     *
     * Called by cleanup code to safely destroy the operation and close the library handle.
     */
    void reset() noexcept;

    DynamicLibraryHandle dlHandle = nullptr; ///< Handle to the loaded shared library.
    CreateFn createFn = nullptr;             ///< Pointer to the operation creation function.
    DeleteFn destroyFn = nullptr;            ///< Pointer to the operation destruction function.
    pek::op::Op *op = nullptr;               ///< Pointer to the allocated operation instance.
};

} // namespace pek::op
