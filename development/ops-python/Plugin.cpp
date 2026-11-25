/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "PythonScriptOp.h"
#include "YoloeInstancePostprocessOp.h"
#include "YoloePromptPrepareOp.h"

#include <string_view>

namespace {

opk::op::Op *createOp(std::string_view opName) {
    if (opName == "PythonScript")
        return new opk::python::PythonScriptOp();
    if (opName == "YoloePromptPrepare") {
        return new opk::python::YoloePromptPrepareOp();
    }
    if (opName == "YoloePromptPack") {
        return new opk::python::YoloePromptPackOp();
    }
    if (opName == "YoloeInstancePostprocess") {
        return new opk::python::YoloeInstancePostprocessOp();
    }
    return nullptr;
}

} // namespace

extern "C" void opk_delete_op_instance(void *opInstance) {
    delete static_cast<opk::op::Op *>(opInstance);
}

extern "C" void *opk_create_op_instance(const char *opName) {
    return opName == nullptr ? nullptr : createOp(opName);
}
