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

#pragma once

#include "op/ExecutionMode.h"
#include "op/Op.h"
#include "op/OpRef.h"

#include <vector>

namespace opk::op {

/** Applies the chain's CPU/GPU preprocessing setting; inference backends stay unchanged. */
class ExecutionSelection {
  public:
    explicit ExecutionSelection(const ExecutionMode preprocessingMode);
    ~ExecutionSelection();
    ExecutionSelection(const ExecutionSelection &) = delete;
    ExecutionSelection &operator=(const ExecutionSelection &) = delete;

    /** Load the chosen implementation; the chain owns the returned Op and its library. */
    Result<OpRef> loadOperation(const std::string &libraryName,
                                const std::string &opName,
                                const std::string &instanceId) const;
    /** Add CPU-selection guidance to GPU failures; preserve errors from other Ops. */
    static Error operationError(const Op &operation, const Error &error);
    /** Start GPU setup after all Ops have been bound. */
    Result<void> prepareDevices(const std::vector<Op *> &ops);
    /** Wait for device setup, or return false while it is pending when waitForDeviceSetup is false.
     */
    Result<bool> prepareFrame(const bool waitForDeviceSetup);

  private:
    struct GpuOperation;
    const ExecutionMode preprocessingMode;
    std::vector<GpuOperation> gpuOperations;
};

} // namespace opk::op
