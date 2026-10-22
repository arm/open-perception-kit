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

#include "op/Op.h"
#include "op/OpChainDescriptor.h"
#include "perf/PipelineSample.h"

#include <memory>

namespace opk::op {

/** Selects preprocessing implementations without replacing inference or stateful Ops. */
class ExecutionSelection {
  public:
    ExecutionSelection();
    ~ExecutionSelection();
    ExecutionSelection(const ExecutionSelection &) = delete;
    ExecutionSelection &operator=(const ExecutionSelection &) = delete;

    Result<void> configure(const OpChainDescriptor &descriptor,
                           const std::vector<Op *> &ops,
                           const std::string &implementationFingerprint);
    /** Supplies whole-pipeline counters at streaming frame boundaries, not per crop. */
    void observePipeline(const perf::PipelineSample &sample);
    /** Counts a frame once regardless of how many inference calls its crops required. */
    void completeFrame(const uint64_t inferenceCalls);
    /** False skips inference while a selected GPU implementation is being prepared. */
    Result<bool> prepareFrame(const OpChainContext &context);
    Result<OpSignal> process(Op &operation, OpChainContext &context);
    /** Join setup and destroy GPU Ops while keeping their libraries loaded for tensor owners. */
    void destroyInstances();

  private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace opk::op
