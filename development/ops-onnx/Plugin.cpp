/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/Op.h"

#include "InferenceOp.h"

#include <cstring>

pek::op::Op *createOp(const std::string &opName) {
    if (opName == "Inference")
        return new pek::onnx::InferenceOp();
    return nullptr;
}

// ---

extern "C" void pek_delete_op_instance(void *opInstacnce) {
    delete (pek::op::Op *)opInstacnce;
}

extern "C" void *pek_create_op_instance(const char *opName) {
    if (!opName)
        return nullptr;
    return createOp(opName);
}