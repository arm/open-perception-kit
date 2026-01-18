#include "op/Op.h"

using namespace amp;

OpRef::OpRef(Op *op) {
    this->op = op;
}

OpRef::~OpRef() {
    delete op;
}
