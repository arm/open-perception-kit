#include "op/Op.h"

#include <cstring>

#include "GenericImagePreprocessOp.h"
#include "GenericPostprocessOp.h"

amp::Op *createOp(const std::string &opName) {
    if (opName == "GenericPostprocess")
        return new amp::GenericPostprocessOp();
    if (opName == "GenericImagePreprocess")
        return new amp::GenericImagePreprocessOp();
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