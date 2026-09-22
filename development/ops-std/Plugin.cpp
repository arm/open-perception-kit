/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/Op.h"

#include <cstring>

#include "GenericImagePreprocessOp.h"
#include "GenericPostprocessOp.h"
#include "InferenceControllerOp.h"

opk::op::Op *createOp(const std::string &opName) {
    if (opName == "GenericPostprocess")
        return new opk::stdop::GenericPostprocessOp();
    if (opName == "GenericImagePreprocess")
        return new opk::stdop::GenericImagePreprocessOp();
    if (opName == "InferenceController")
        return new opk::stdop::InferenceControllerOp();
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