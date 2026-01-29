#include "op/Op.h"

#include <cstring>

#include "ImagePreprocessOp.h"
#include "Postprocess.h"

amp::Op *createOp(const std::string &opName) {
    if (opName == "Postprocess")
        return new amp::Postprocess();
    if (opName == "ImagePreprocess")
        return new amp::ImagePreprocessOp();
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