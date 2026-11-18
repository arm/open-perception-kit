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

#include "TextDisplay.h"
#include "opk/AudioResults.h"
#include <gtest/gtest.h>

TEST(MetadataTextDisplay, UnknownBitmapTypesAreNotReinterpretedAsBytes) {
    open_perception_kit::metadata::SegmentationMasksT payload;
    auto mask = std::make_unique<open_perception_kit::metadata::SegmentationMaskT>();
    mask->bitmap = std::make_unique<open_perception_kit::metadata::BitmapDataT>();
    mask->bitmap->encoding = static_cast<open_perception_kit::metadata::BitmapEncoding>(99);
    mask->bitmap->pixels = {1, 2, 3, 4};
    payload.masks.push_back(std::move(mask));
    const auto text = TextDisplay::formatText(payload);
    EXPECT_NE(text.find("invalid-bitmap"), std::string::npos);
    EXPECT_EQ(text.find("min="), std::string::npos);
}

TEST(MetadataTextDisplay, PresenceShowsRawScoresRatherThanInventedProbabilities) {
    open_perception_kit::metadata::PersonPresencesT payload;
    auto presence = std::make_unique<open_perception_kit::metadata::PersonPresenceT>();
    presence->result = std::make_unique<open_perception_kit::metadata::PersonPresenceResultT>();
    presence->result->person_present = true;
    presence->result->yes_score = 2;
    presence->result->no_score = -1;
    payload.presences.push_back(std::move(presence));
    const auto text = TextDisplay::formatText(payload);
    EXPECT_NE(text.find("outcome=person"), std::string::npos);
    EXPECT_NE(text.find("raw_yes=2.000 raw_no=-1.000"), std::string::npos);
}

TEST(MetadataTextDisplay, AudioMissingAndMeasuredZeroRemainDistinct) {
    open_perception_kit::metadata::VoiceActivityDetectionsT payload;
    auto value = std::make_unique<open_perception_kit::metadata::VoiceActivityDetectionT>();
    value->window_end_sample = 512;
    EXPECT_FALSE(opk::audio_results::valid(*value));
    value->speech_probability = 0;
    value->speech_active = false;
    value->window_start_ts_ns = 0;
    value->window_end_ts_ns = 32000000;
    EXPECT_TRUE(opk::audio_results::valid(*value));
    payload.detections.push_back(std::move(value));
    const auto text = TextDisplay::formatText(payload);
    EXPECT_NE(text.find("active=false probability=0.000"), std::string::npos);
    EXPECT_NE(text.find("pts=[0,32000000)"), std::string::npos);
    payload.detections[0]->speech_active = flatbuffers::nullopt;
    EXPECT_NE(TextDisplay::formatText(payload).find("unavailable"), std::string::npos);
}

TEST(MetadataTextDisplay, EmbeddingsExposeComparisonSpaceAndExplicitSubject) {
    open_perception_kit::metadata::ObjectEmbeddingsT payload;
    auto value = std::make_unique<open_perception_kit::metadata::ObjectEmbeddingT>();
    value->subject_id = 7;
    value->values = {1, 0};
    value->space = std::make_unique<open_perception_kit::metadata::EmbeddingSpaceT>();
    value->space->id = "test:space:v1";
    value->space->dimensions = 2;
    value->space->metric = open_perception_kit::metadata::EmbeddingMetric::Cosine;
    payload.embeddings.push_back(std::move(value));
    const auto text = TextDisplay::formatText(payload);
    EXPECT_NE(text.find("subject=7 space=test:space:v1 dimensions=2 metric=Cosine usable=true"),
              std::string::npos);
}
