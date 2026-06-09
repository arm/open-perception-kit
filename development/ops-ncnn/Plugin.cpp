/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/Op.h"

#include "InferenceOp.h"

#include <cstring>

pek::op::Op *createOp(const std::string &opName) {
    if (opName == "Inference")
        return new pek::ncnnrt::InferenceOp();
    return nullptr;
}

// ---

extern "C" void pek_delete_op_instance(void *opInstance) {
    delete static_cast<pek::op::Op *>(opInstance);
}

extern "C" void *pek_create_op_instance(const char *opName) {
    if (!opName)
        return nullptr;
    return createOp(opName);
}
