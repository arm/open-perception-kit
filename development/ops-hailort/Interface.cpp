#include "op/Op.h"

#include "PreprocessInference.h"

#include <cstring>

amp::Op *createOp(const std::string &opName) {
    if (opName == "PreprocessInference")
        return new hailort::PreprocessInference();
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