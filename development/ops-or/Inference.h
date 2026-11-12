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

#include "opk/Model.h"
#include "opk/ModelDescriptor.h"
#include "opk/Result.h"

#include <memory>

namespace opk::or_runtime {

class Inference {
  public:
    Inference();
    ~Inference();

    opk::Result<void> setupFromJson(const std::string &path);
    opk::Result<void> inference();
    const opk::Model &getModel() const;
    uint8_t *getInputTensorDataAddress(const size_t index) const;
    const uint8_t *getOutputTensorDataAddress(const size_t index) const;
    opk::Shape getOutputTensorFinalShape(const size_t index) const;

  private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace opk::or_runtime
