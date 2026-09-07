/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "PythonScriptOp.h"

#include <string_view>

namespace {

pek::op::Op *createOp(std::string_view opName) {
    if (opName == "PythonScript")
        return new pek::python::PythonScriptOp();
    return nullptr;
}

} // namespace

extern "C" void pek_delete_op_instance(void *opInstance) {
    delete static_cast<pek::op::Op *>(opInstance);
}

extern "C" void *pek_create_op_instance(const char *opName) {
    return opName == nullptr ? nullptr : createOp(opName);
}
