/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include <string_view>

namespace {

opk::op::Op *createOp(std::string_view opName) {
    if (opName == "PythonScript")
        return new opk::python::PythonScriptOp();
    return nullptr;
}

} // namespace

extern "C" void opk_delete_op_instance(void *opInstance) {
    delete static_cast<opk::op::Op *>(opInstance);
}

extern "C" void *opk_create_op_instance(const char *opName) {
    return opName == nullptr ? nullptr : createOp(opName);
}
