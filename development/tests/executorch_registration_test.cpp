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

#include <executorch/runtime/kernel/operator_registry.h>
#include <gtest/gtest.h>

TEST(ExecuTorchRegistration, RetainsPrimitiveAndPortableOperators) {
    // MobileSAM uses this primitive outside its delegated XNNPACK regions.
    EXPECT_TRUE(executorch::runtime::registry_has_op_function("executorch_prim::et_view.default"));
    EXPECT_TRUE(executorch::runtime::registry_has_op_function("aten::native_layer_norm.out"));
}
