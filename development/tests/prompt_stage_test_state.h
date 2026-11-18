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

#include <cstddef>
#include <memory>
#include <vector>

struct PromptStageTestState {
    size_t calls = 0;
    size_t breakAfter = 0;
    bool preparePrompt = true;
    bool error = false;
    bool produceOutput = true;
    bool validOutput = true;
    bool sawAssociationDuringInference = false;
    bool sawPreparedAfterInference = false;
    opk::op::OpSignal signal = opk::op::OpSignal::Continue;
    std::vector<std::shared_ptr<const opk::PromptSnapshot>> observed;
};
