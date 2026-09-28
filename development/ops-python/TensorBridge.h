/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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
