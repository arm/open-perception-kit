/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <Python.h>

#include "op/OpChainContext.h"
#include "opk/Model.h"

namespace opk::python {

void appendTensorModuleInittab();
void initializeTensorModule();
PyObject *wrapTensors(const opk::op::OpChainContext &context, const opk::Model *model);
PyObject *wrapContext(PyObject *producerInfo);

} // namespace opk::python
