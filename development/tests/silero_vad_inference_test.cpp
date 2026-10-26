/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "InferenceOp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>

TEST(SileroVadInference, FeedbackAndDiscontinuityReset) {
    const std::filesystem::path descriptor = SILERO_MODEL_DESCRIPTOR;
    ASSERT_TRUE(std::filesystem::is_regular_file(descriptor.parent_path() / "model_int8.onnx"))
        << "Silero model artifact is missing. Run: "
           "python3 scripts/download-models.py --models-dir config/models/silero-vad";
    opk::onnx::InferenceOp inference;
    opk::AttributeMap attributes;
    attributes.set("modelDescriptor", descriptor.string());
    const auto configured = inference.configure(attributes);
    ASSERT_TRUE(configured) << (configured ? "" : configured.error().toString());
    ASSERT_EQ(inference.getModel().inputs[0].dataKind, opk::DataKind::AudioPcm);
    std::array<float, 576> pcm;
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = 0.2f * std::sin(i * 0.17f);
    std::memcpy(inference.getTensorDataAddress(0), pcm.data(), sizeof(pcm));
    opk::op::OpChainContext ctx;
    ctx.inferenceInfo.audio.discontinuity = true;
    ASSERT_TRUE(inference.process(ctx));
    int64_t sampleRate = 0;
    std::memcpy(&sampleRate, inference.getTensorDataAddress(2), sizeof(sampleRate));
    EXPECT_EQ(sampleRate, 16000);
    EXPECT_EQ(inference.getModel().inputs[2].shape.rank, 0u);
    EXPECT_EQ(ctx.inferenceOutputTensors[0].getShape(), opk::Shape(1, 1));
    EXPECT_GE(ctx.inferenceOutputTensors[0].get(0), 0.0f);
    EXPECT_LE(ctx.inferenceOutputTensors[0].get(0), 1.0f);
    std::array<float, 256> initialH;
    std::memcpy(initialH.data(), inference.getTensorDataAddress(1), sizeof(initialH));
    EXPECT_TRUE(std::ranges::any_of(initialH, [](float x) { return x != 0; }));
    ctx.inferenceInfo.audio.discontinuity = false;
    ASSERT_TRUE(inference.process(ctx));
    std::array<float, 256> currentH;
    std::memcpy(currentH.data(), inference.getTensorDataAddress(1), sizeof(currentH));
    EXPECT_NE(initialH, currentH);
    ctx.inferenceInfo.audio.discontinuity = true;
    ASSERT_TRUE(inference.process(ctx));
    std::memcpy(currentH.data(), inference.getTensorDataAddress(1), sizeof(currentH));
    EXPECT_EQ(initialH, currentH);
    std::array<float, 576> currentPcm;
    std::memcpy(currentPcm.data(), inference.getTensorDataAddress(0), sizeof(currentPcm));
    EXPECT_EQ(pcm, currentPcm);
}
