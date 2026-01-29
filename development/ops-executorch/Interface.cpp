#include "op/Op.h"

#include "InferenceOp.h"

#include <cstring>

amp::Op *createOp(const std::string &opName) {
    if (opName == "Inference")
        return new exct::InferenceOp();
    return nullptr;
}

// ---

extern "C" void amp_delete_op_instance(void *opInstacnce) {
    delete (amp::Op *)opInstacnce;
}

extern "C" void *amp_create_op_instance(const char *opName) {
    if (!opName)
        return nullptr;
    return createOp(opName);
}