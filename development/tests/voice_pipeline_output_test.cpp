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

#include "KeywordOutput.h"

#include <array>
#include <gtest/gtest.h>
#include <memory>
#include <span>
#include <string>
#include <utility>

TEST(VoicePipelineOutput, QuotesLabelsAndEscapesTerminalControlsAfterSerialization) {
    const std::array<std::pair<std::string, std::string>, 6> labels{{
        {"go", "\"go\""},
        {std::string("go\0stop", 7), R"("go\x00stop")"},
        {"go\nstop", R"("go\nstop")"},
        {"\033[31mgo", R"("\x1b[31mgo")"},
        {"go\\stop\"", R"("go\\stop\"")"},
        {"árvíz", "\"árvíz\""},
    }};
    for (const auto &[label, expected] : labels) {
        SCOPED_TRACE(expected);
        open_perception_kit::metadata::ClassificationsT payload;
        payload.layer = std::make_unique<open_perception_kit::metadata::LayerInfoT>();
        payload.layer->infer_element_id = "keywords";
        payload.layer->producer = std::make_unique<open_perception_kit::metadata::ProducerInfoT>();
        payload.layer->producer->instance_id = "keywords/opk-std-ops-GenericPostprocess-0";
        auto classification = std::make_unique<open_perception_kit::metadata::ClassificationT>();
        classification->kind = open_perception_kit::metadata::ClassificationKind::Categorical;
        classification->label_set = "test:keywords-v1";
        classification->object = std::make_unique<open_perception_kit::metadata::ObjectMetaT>();
        auto candidate =
            std::make_unique<open_perception_kit::metadata::ClassificationCandidateT>();
        candidate->text = label;
        candidate->class_id = 0;
        candidate->confidence = 0.75f;
        classification->candidates.push_back(std::move(candidate));
        payload.classifications.push_back(std::move(classification));
        open_perception_kit::container::envelope results;
        results.add(payload);
        const auto bytes = results.serialize();
        const open_perception_kit::container::envelope decoded{
            std::span<const uint8_t>{bytes.data(), bytes.size()}};
        ASSERT_TRUE(decoded.valid());

        testing::internal::CaptureStdout();
        voice_pipeline::printKeywords(decoded);
        EXPECT_EQ(testing::internal::GetCapturedStdout(),
                  "\r\033[2KKEYWORD " + expected + " (75.0%)\n");
    }
}

TEST(VoicePipelineOutput, EmptyOrInvalidAudioAssessmentsDoNotProduceKeywordEvents) {
    using namespace open_perception_kit::metadata;
    for (int mode = 0; mode < 4; ++mode) {
        ClassificationsT payload;
        payload.layer = std::make_unique<LayerInfoT>();
        payload.layer->producer = std::make_unique<ProducerInfoT>();
        payload.layer->producer->instance_id = "keywords/opk-std-ops-GenericPostprocess-0";
        auto value = std::make_unique<ClassificationT>();
        value->kind = ClassificationKind::Categorical;
        value->label_set = "test:commands";
        opk::AudioInferenceMetadata audio;
        audio.sourceId = "keywords/pcm";
        audio.sessionId = "session";
        audio.segmentIndex = 1;
        audio.sampleRateHz = 16000;
        audio.windowEndSample = 512;
        value->audio_window = opk::audio_results::makeWindow(audio);
        if (mode != 0) {
            auto candidate = std::make_unique<ClassificationCandidateT>();
            candidate->class_id = 0;
            candidate->confidence = 0;
            candidate->text = "open";
            value->candidates.push_back(std::move(candidate));
        }
        if (mode == 2)
            value->audio_window->sample_rate_hz = 0;
        if (mode == 3)
            value->subject_id = 1;
        payload.classifications.push_back(std::move(value));
        open_perception_kit::container::envelope results;
        results.add(payload);
        const auto bytes = results.serialize();
        const open_perception_kit::container::envelope decoded{std::span<const uint8_t>(bytes)};
        testing::internal::CaptureStdout();
        voice_pipeline::printKeywords(decoded);
        EXPECT_EQ(testing::internal::GetCapturedStdout(),
                  mode == 1 ? "\r\033[2KKEYWORD \"open\" (0.0%)\n" : "");
    }
}
