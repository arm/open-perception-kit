/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/Op.h"

#include "InferenceOp.h"

#include <cstring>

pek::Op *createOp(const std::string &opName) {
    if (opName == "Inference")
        return new pek::extrch::InferenceOp();
    return nullptr;
}

// ---

extern "C" void pek_delete_op_instance(void *opInstacnce) {
    delete (pek::Op *)opInstacnce;
}

extern "C" void *pek_create_op_instance(const char *opName) {
    if (!opName)
        return nullptr;
    return createOp(opName);
}