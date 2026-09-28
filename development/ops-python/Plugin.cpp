/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "PythonScriptOp.h"

#include <string_view>

namespace {

opk::op::Op *createOp(std::string_view opName) {
    if (opName == "PythonScript")
        return new opk::python::PythonScriptOp();
    return nullptr;
}

} // namespace

extern "C" void opk_delete_op_instance(void *opInstance) {
    delete static_cast<opk::op::Op *>(opInstance);
}

extern "C" void *opk_create_op_instance(const char *opName) {
    return opName == nullptr ? nullptr : createOp(opName);
}
