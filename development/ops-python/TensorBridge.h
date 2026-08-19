/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <Python.h>

#include "op/OpChainContext.h"
#include "pek/Model.h"

namespace pek::python {

void appendTensorModuleInittab();
PyObject *wrapTensors(const pek::op::OpChainContext &context, const pek::Model *model);
PyObject *wrapContext(PyObject *producerInfo);

} // namespace pek::python
