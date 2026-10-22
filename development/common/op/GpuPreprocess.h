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

#include "opk/Result.h"

#include <string>

namespace opk::op {

/** Reports the device and transfers used by an optional GPU preprocessing Op. */
struct OpInterfaceGpuPreprocess {
    virtual ~OpInterfaceGpuPreprocess() = default;

    /** Prepare device resources after bind(), without accessing inference-owned state. */
    virtual Result<void> prepareDevice() = 0;
    /** Hardware and driver identity used in the execution log. */
    virtual std::string identity() const = 0;
    /** Whether the last successful frame required a CPU upload of source pixels. */
    virtual bool inputCopied() const = 0;
    /** Whether the last successful tensor required readback or a backend input copy. */
    virtual bool tensorCopied() const = 0;
};

} // namespace opk::op
