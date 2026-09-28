/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "op/Op.h"

#include "InferenceOp.h"

#include <cstring>

opk::op::Op *createOp(const std::string &opName) {
    if (opName == "Inference")
        return new opk::extrch::InferenceOp();
    return nullptr;
}

// ---

extern "C" void opk_delete_op_instance(void *opInstacnce) {
    delete (opk::op::Op *)opInstacnce;
}

extern "C" void *opk_create_op_instance(const char *opName) {
    if (!opName)
        return nullptr;
    return createOp(opName);
}