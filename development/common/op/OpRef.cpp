/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/OpRef.h"
#include "op/Op.h"

#include <fmt/format.h>
#include <utility> // std::exchange

using namespace pek::op;

OpRef::OpRef() = default;

void OpRef::reset() noexcept {
    // Destroy op first (may reference code/data in the library)
    if (op && destroyFn) {
        // best-effort: ignore exceptions (destroyFn should not throw)
        destroyFn(op);
    }
    op = nullptr;

    // Close library after destroying op
    if (dlHandle) {
        Tools::DynamicLibraryClose(dlHandle);
    }
    dlHandle = nullptr;

    // Clear function pointers
    createFn = nullptr;
    destroyFn = nullptr;
}

OpRef::~OpRef() {
    reset();
}

OpRef::OpRef(OpRef &&other) noexcept
    : dlHandle(std::exchange(other.dlHandle, nullptr)),
      createFn(std::exchange(other.createFn, nullptr)),
      destroyFn(std::exchange(other.destroyFn, nullptr)), op(std::exchange(other.op, nullptr)) {}

OpRef &OpRef::operator=(OpRef &&other) noexcept {
    if (this != &other) {
        reset();

        dlHandle = std::exchange(other.dlHandle, nullptr);
        createFn = std::exchange(other.createFn, nullptr);
        destroyFn = std::exchange(other.destroyFn, nullptr);
        op = std::exchange(other.op, nullptr);
    }
    return *this;
}

pek::Result<void> OpRef::bind(const std::string &soName, const std::string &opName) {
    // Ensure a previous binding is cleanly released
    reset();

    // Open library (Tools::DynamicLibraryOpen returns tl::optional / similar)
    auto openResult = Tools::DynamicLibraryOpen(soName);
    if (!openResult.has_value()) {
        return tl::make_unexpected(PEK_ERROR(pek::ErrorFlag::SystemFailure,
                                             fmt::format("Cannot load library [{}]", soName)));
    }

    dlHandle = *openResult;

    auto cr = Tools::DynamicLibraryGetSymbol<CreateFn>(dlHandle, "pek_create_op_instance");
    auto del = Tools::DynamicLibraryGetSymbol<DeleteFn>(dlHandle, "pek_delete_op_instance");
    if (!cr || !del) {
        // reset() will close dlHandle and clear fields
        reset();
        return tl::make_unexpected(
            PEK_ERROR(pek::ErrorFlag::SystemFailure,
                      fmt::format("Cannot get interface methods of library [{}]", soName)));
    }

    createFn = *cr;
    destroyFn = *del;

    // create instance
    void *raw = nullptr;
    try {
        raw = createFn(opName.c_str());
    } catch (...) {
        // In case createFn throws (shouldn't), clean up
        reset();
        return tl::make_unexpected(PEK_ERROR(
            pek::ErrorFlag::SystemFailure,
            fmt::format("Exception while creating op [{}] of library [{}]", opName, soName)));
    }

    if (!raw) {
        reset();
        return tl::make_unexpected(
            PEK_ERROR(pek::ErrorFlag::SystemFailure,
                      fmt::format("Cannot create op [{}] of library [{}]", opName, soName)));
    }

    op = static_cast<pek::op::Op *>(raw);
    return {}; // success
}