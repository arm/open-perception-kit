/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/Op.h"

#include "InferenceOp.h"

#include <cstring>

opk::op::Op *createOp(const std::string &opName) {
    if (opName == "Inference")
        return new opk::extrch::InferenceOp();
    return nullptr;
}

// ---

extern "C" void opk_delete_op_instance(void *opInstacnce) {
    delete (opk::op::Op *)opInstacnce;
}

extern "C" void *opk_create_op_instance(const char *opName) {
    if (!opName)
        return nullptr;
    return createOp(opName);
}