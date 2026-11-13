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

#include "opk/AttributeMap.h"
#include "opk/Model.h"
#include "opk/PcmRingBuffer.h"
#include "opk/Result.h"
#include <memory>

namespace opk::stdop::preproc {

/// Binds one audio conversion and owns its state; never retains borrowed PCM views.
class GenericAudioTensorBuilder {
  public:
    GenericAudioTensorBuilder();
    ~GenericAudioTensorBuilder();
    opk::Result<void> configure(const opk::AttributeMap &attributes);
    opk::Result<void> bind(const opk::ModelInput &input);
    void resetBinding();
    size_t getRequiredAudioSamples() const;
    opk::Result<void> build(const PcmRingBuffer::View &pcm,
                            uint8_t *const destination,
                            const size_t hopSampleCount,
                            const bool discontinuity);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace opk::stdop::preproc
