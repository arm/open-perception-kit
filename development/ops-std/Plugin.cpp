/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/Op.h"

#include <cstring>

#include "GenericImagePreprocessOp.h"
#include "GenericPostprocessOp.h"
#include "InferenceControllerOp.h"

pek::op::Op *createOp(const std::string &opName) {
    if (opName == "GenericPostprocess")
        return new pek::stdop::GenericPostprocessOp();
    if (opName == "GenericImagePreprocess")
        return new pek::stdop::GenericImagePreprocessOp();
    if (opName == "InferenceController")
        return new pek::stdop::InferenceControllerOp();
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