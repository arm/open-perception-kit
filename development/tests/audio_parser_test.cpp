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

#include "postproc/KeywordClassificationParser.h"
#include "postproc/VoiceActivityParser.h"

#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

namespace {

using Results = open_perception_kit::FrameResults;
using VoicePayload = open_perception_kit::metadata::VoiceActivityDetectionsT;
using KeywordPayload = open_perception_kit::metadata::ClassificationsT;

struct VoiceFixture {
    opk::stdop::postproc::VoiceActivityParser parser;
    opk::AttributeMap attributes;
    std::array<float, 6> scores{};
    opk::TensorView tensor{
        scores.data(), sizeof(scores), opk::Shape(1, 2, 3), opk::Dtype::Float32, 1, 0};
    opk::TensorParser::Input input{attributes};
    Results results;

    VoiceFixture() {
        attributes.set("outputTensorIndex", int64_t{2});
        attributes.set("scoreIndex", int64_t{4});
        attributes.set("activation", "none");
        attributes.set("thresholdOn", 0.75);
        attributes.set("thresholdOff", 0.25);
        attributes.set("attackMs", int64_t{0});
        attributes.set("releaseMs", int64_t{0});
        input.tensors[2] = &tensor;
        input.inferenceInfo.modelName = "synthetic-speech-scores";
        input.inferenceInfo.inferElementId = "speech-stream";
        input.inferenceInfo.parentId = 17;
        input.producerInfo.instance_id = "speech-stream/post";
        input.producerInfo.implementation = "VoiceActivityParser";
        input.inferenceInfo.audio.hopSampleCount = 160;
        input.inferenceInfo.audio.windowStartSample = 160;
        input.inferenceInfo.audio.windowEndSample = 800;
        input.inferenceInfo.audio.windowStartTimestampNs = 10'000'000;
        input.inferenceInfo.audio.windowEndTimestampNs = 50'000'000;
    }

    bool step(const float score, const bool discontinuity = false) {
        scores[4] = score;
        input.inferenceInfo.audio.discontinuity = discontinuity;
        const auto previousSize = results.size();
        const auto parsed = parser.parse(input, results);
        EXPECT_TRUE(parsed) << (parsed ? "" : parsed.error().toString());
        if (!parsed)
            return false;
        bool active = false;
        size_t count = 0;
        results.for_each<VoicePayload>([&](const auto &payload) {
            ++count;
            EXPECT_EQ(payload.detections.size(), 1u);
            if (!payload.detections.empty()) {
                EXPECT_TRUE(payload.detections[0]->speech_active.has_value());
                active = payload.detections[0]->speech_active.value_or(false);
            }
        });
        EXPECT_EQ(count, previousSize + 1);
        auto &audio = input.inferenceInfo.audio;
        audio.windowStartSample += audio.hopSampleCount;
        audio.windowEndSample += audio.hopSampleCount;
        if (audio.windowStartTimestampNs && audio.windowEndTimestampNs) {
            *audio.windowStartTimestampNs += audio.hopSampleCount * 62'500;
            *audio.windowEndTimestampNs += audio.hopSampleCount * 62'500;
        }
        return active;
    }
};

TEST(VoiceActivityParser, SelectsConfiguredFlatScoreAndPreservesAudioIdentity) {
    VoiceFixture fixture;
    fixture.scores[4] = 0.875f;
    fixture.input.inferenceInfo.audio.discontinuity = true;
    Results results;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, results));
    size_t count = 0;
    results.for_each<VoicePayload>([&](const auto &payload) {
        ++count;
        EXPECT_EQ(payload.layer->model, "synthetic-speech-scores");
        EXPECT_EQ(payload.layer->infer_element_id, "speech-stream");
        ASSERT_TRUE(payload.layer->producer);
        EXPECT_EQ(payload.layer->producer->instance_id, "speech-stream/post");
        EXPECT_EQ(payload.layer->producer->implementation, "VoiceActivityParser");
        ASSERT_EQ(payload.detections.size(), 1u);
        const auto &detection = *payload.detections[0];
        EXPECT_FLOAT_EQ(detection.speech_probability.value_or(-1), 0.875f);
        EXPECT_EQ(detection.speech_active, true);
        EXPECT_EQ(detection.window_start_sample, 160u);
        EXPECT_EQ(detection.window_end_sample, 800u);
        ASSERT_TRUE(detection.window_start_ts_ns);
        ASSERT_TRUE(detection.window_end_ts_ns);
        EXPECT_EQ(*detection.window_start_ts_ns, 10'000'000u);
        EXPECT_EQ(*detection.window_end_ts_ns, 50'000'000u);
        EXPECT_TRUE(detection.discontinuity);
    });
    EXPECT_EQ(count, 1u);
}

TEST(VoiceActivityParser, AppliesOnlyTheConfiguredActivation) {
    VoiceFixture fixture;
    EXPECT_TRUE(fixture.step(0.75f));
    EXPECT_FALSE(fixture.step(0.25f));
    fixture.attributes.set("activation", "sigmoid");
    for (const float score : {-1000.0f, -2.0f, 2.0f, 1000.0f}) {
        fixture.scores[4] = score;
        Results results;
        ASSERT_TRUE(fixture.parser.parse(fixture.input, results));
        ASSERT_EQ(results.size(), 1u);
        results.for_each<VoicePayload>([&](const auto &payload) {
            const float probability = payload.detections[0]->speech_probability.value_or(-1);
            EXPECT_TRUE(std::isfinite(probability));
            EXPECT_NEAR(probability, 1.0 / (1.0 + std::exp(-static_cast<double>(score))), 1e-6);
        });
    }
}

TEST(VoiceActivityParser, UsesConfiguredDurationsAndPreservesStateOnInvalidInput) {
    VoiceFixture fixture;
    fixture.attributes.set("attackMs", int64_t{20});
    fixture.attributes.set("releaseMs", int64_t{30});
    EXPECT_FALSE(fixture.step(0.75f));
    EXPECT_TRUE(fixture.step(0.75f));
    EXPECT_TRUE(fixture.step(0.5f));
    EXPECT_TRUE(fixture.step(0.25f));
    fixture.scores[4] = std::numeric_limits<float>::quiet_NaN();
    fixture.input.inferenceInfo.audio.discontinuity = true;
    Results invalidResults;
    EXPECT_FALSE(fixture.parser.parse(fixture.input, invalidResults));
    EXPECT_EQ(invalidResults.size(), 0u);
    EXPECT_TRUE(fixture.step(0.25f));
    EXPECT_FALSE(fixture.step(0.25f));
    EXPECT_FALSE(fixture.step(0.75f));
    EXPECT_FALSE(fixture.step(0.75f, true));
    EXPECT_TRUE(fixture.step(0.75f));
}

TEST(VoiceActivityParser, RequiresConsecutiveEvidenceAndRetainsBurstResults) {
    VoiceFixture fixture;
    fixture.attributes.set("attackMs", int64_t{30});
    fixture.attributes.set("releaseMs", int64_t{30});
    const std::array<std::pair<float, bool>, 13> sequence{{
        {0.9f, false},
        {0.9f, false},
        {0.5f, false}, // Interrupt the attack run.
        {0.9f, false},
        {0.9f, false},
        {0.9f, true},
        {0.5f, true}, // The hysteresis band preserves active speech.
        {0.1f, true},
        {0.1f, true},
        {0.5f, true}, // Interrupt the release run.
        {0.1f, true},
        {0.1f, true},
        {0.1f, false},
    }};
    for (const auto &[score, active] : sequence)
        EXPECT_EQ(fixture.step(score), active);
    size_t count = 0;
    fixture.results.for_each<VoicePayload>([&](const auto &payload) {
        ASSERT_LT(count, sequence.size());
        EXPECT_FLOAT_EQ(payload.detections[0]->speech_probability.value_or(-1),
                        sequence[count].first);
        ++count;
    });
    EXPECT_EQ(count, sequence.size());
}

TEST(VoiceActivityParser, RoundsDurationsUpToCompleteAudioHops) {
    VoiceFixture fixture;
    fixture.attributes.set("thresholdOn", 0.6);
    fixture.attributes.set("thresholdOff", 0.45);
    fixture.attributes.set("attackMs", int64_t{50});
    fixture.attributes.set("releaseMs", int64_t{50});
    fixture.input.inferenceInfo.audio.hopSampleCount = 512;
    EXPECT_FALSE(fixture.step(0.9f));
    EXPECT_TRUE(fixture.step(0.9f)); // 50 ms = 800 samples; two 512-sample hops.
    EXPECT_TRUE(fixture.step(0.1f));
    EXPECT_FALSE(fixture.step(0.1f));
}

TEST(VoiceActivityParser, ResetsActiveStateAndKeepsInstancesIndependent) {
    VoiceFixture first, second;
    for (auto *const fixture : {&first, &second}) {
        fixture->attributes.set("attackMs", int64_t{20});
        fixture->attributes.set("releaseMs", int64_t{20});
    }
    EXPECT_FALSE(first.step(0.9f));
    EXPECT_TRUE(first.step(0.9f));
    EXPECT_FALSE(second.step(0.9f));
    EXPECT_FALSE(first.step(0.9f, true));
    EXPECT_TRUE(second.step(0.9f));
    EXPECT_TRUE(first.step(0.9f));
    EXPECT_FALSE(first.step(0.1f, true));
    EXPECT_TRUE(second.step(0.5f));
}

TEST(VoiceActivityParser, RoundTripsZeroAndAbsentTimestamps) {
    VoiceFixture fixture;
    auto &audio = fixture.input.inferenceInfo.audio;
    audio.windowStartSample = 0;
    audio.windowEndSample = 640;
    audio.windowStartTimestampNs = 0;
    audio.windowEndTimestampNs = 40'000'000;
    EXPECT_TRUE(fixture.step(0.875f, true));
    audio.windowStartTimestampNs.reset();
    audio.windowEndTimestampNs.reset();
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.results));
    const auto bytes = open_perception_kit::serialize(fixture.results);
    const Results decoded{std::span<const uint8_t>(bytes)};
    size_t count = 0;
    decoded.for_each<VoicePayload>([&](const auto &payload) {
        ASSERT_EQ(payload.detections.size(), 1u);
        const auto &detection = *payload.detections[0];
        if (count++ == 0) {
            ASSERT_TRUE(detection.window_start_ts_ns);
            ASSERT_TRUE(detection.window_end_ts_ns);
            EXPECT_EQ(*detection.window_start_ts_ns, 0u);
            EXPECT_EQ(*detection.window_end_ts_ns, 40'000'000u);
            EXPECT_TRUE(detection.discontinuity);
            EXPECT_EQ(detection.window_start_sample, 0u);
            EXPECT_EQ(detection.window_end_sample, 640u);
        } else {
            EXPECT_FALSE(detection.window_start_ts_ns);
            EXPECT_FALSE(detection.window_end_ts_ns);
        }
    });
    EXPECT_EQ(count, 2u);
}

TEST(VoiceActivityDetections, EmptyPayloadAndOptionalLayerRoundTrip) {
    Results results;
    results.add(VoicePayload{});
    const auto bytes = open_perception_kit::serialize(results);
    const Results decoded{std::span<const uint8_t>(bytes)};
    const auto payload = decoded.get<VoicePayload>();
    ASSERT_TRUE(payload.has_value());
    EXPECT_TRUE(payload->value().detections.empty());
    EXPECT_FALSE(payload->value().layer);
}

TEST(VoiceActivityParser, RequiresEveryDecisionAndSelectionParameter) {
    VoiceFixture fixture;
    const auto complete = fixture.attributes.cloneDeep();
    for (const auto *const name : {"outputTensorIndex",
                                   "scoreIndex",
                                   "activation",
                                   "thresholdOn",
                                   "thresholdOff",
                                   "attackMs",
                                   "releaseMs"}) {
        SCOPED_TRACE(name);
        fixture.attributes = complete.cloneDeep();
        fixture.attributes.raw().erase(name);
        Results results;
        EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
        EXPECT_EQ(results.size(), 0u);
    }
}

TEST(VoiceActivityParser, RejectsInvalidSelectionPolicyScoresAndStorage) {
    VoiceFixture fixture;
    const auto complete = fixture.attributes.cloneDeep();
    const auto invalidAttributes = std::to_array<std::pair<const char *, opk::AttributeValue>>(
        {{"outputTensorIndex", -1},
         {"outputTensorIndex", 16},
         {"outputTensorIndex", 2.0},
         {"scoreIndex", -1},
         {"scoreIndex", 6},
         {"scoreIndex", 4.0},
         {"activation", "softmax"},
         {"activation", 1},
         {"thresholdOn", 1.1},
         {"thresholdOn", "0.75"},
         {"thresholdOff", 0.8},
         {"thresholdOff", -0.1},
         {"thresholdOff", std::numeric_limits<double>::infinity()},
         {"attackMs", -1},
         {"attackMs", 1.0},
         {"releaseMs", -1},
         {"releaseMs", std::numeric_limits<int64_t>::max()}});
    for (const auto &[name, value] : invalidAttributes) {
        SCOPED_TRACE(name);
        fixture.attributes = complete.cloneDeep();
        fixture.attributes[name] = value;
        Results results;
        EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
        EXPECT_EQ(results.size(), 0u);
    }
    fixture.attributes = complete.cloneDeep();
    for (const float score : {-0.1f,
                              1.1f,
                              std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity()}) {
        fixture.scores[4] = score;
        Results results;
        EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
        EXPECT_EQ(results.size(), 0u);
    }
    fixture.scores[4] = 0.8f;
    fixture.input.tensors[2] = nullptr;
    Results results;
    EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
    fixture.input.tensors[2] = &fixture.tensor;
    fixture.tensor = opk::TensorView(
        fixture.scores.data(), sizeof(float), opk::Shape(1, 2, 3), opk::Dtype::Float32, 1, 0);
    EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
    fixture.tensor = opk::TensorView(
        fixture.scores.data(), sizeof(fixture.scores), opk::Shape(1, 2, 3), opk::Dtype::Int8, 1, 0);
    EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
    EXPECT_EQ(results.size(), 0u);
}

TEST(VoiceActivityParser, RejectsInvalidAudioWindowsAndTimestamps) {
    VoiceFixture fixture;
    const auto valid = fixture.input.inferenceInfo.audio;
    std::array invalid{valid, valid, valid, valid, valid, valid};
    invalid[0].hopSampleCount = 0;
    invalid[1].hopSampleCount = 641;
    invalid[2].windowEndSample = valid.windowStartSample;
    invalid[3].windowStartTimestampNs.reset();
    invalid[4].windowEndTimestampNs = valid.windowStartTimestampNs;
    invalid[5].windowEndTimestampNs = 9'000'000;
    for (const auto &audio : invalid) {
        fixture.input.inferenceInfo.audio = audio;
        Results results;
        EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
        EXPECT_EQ(results.size(), 0u);
    }
}

struct KeywordFixture {
    opk::stdop::postproc::KeywordClassificationParser parser;
    opk::AttributeMap attributes;
    std::array<float, 4> scores{};
    opk::TensorView tensor{
        scores.data(), sizeof(scores), opk::Shape(1, 1, 4), opk::Dtype::Float32, 1, 0};
    opk::TensorParser::Input input{attributes};

    KeywordFixture() {
        attributes.set("outputTensorIndex", int64_t{1});
        attributes.set("activation", "softmax");
        attributes.set("confidenceThreshold", 0.6);
        attributes.setArray("labels", {"open", nullptr, "close", "hold"});
        attributes.set("labelSet", "test:commands-v1");
        input.tensors[1] = &tensor;
        input.inferenceInfo.modelName = "synthetic-command-scores";
        input.inferenceInfo.inferElementId = "commands";
        input.inferenceInfo.parentId = 29;
        input.producerInfo.instance_id = "commands/post";
        input.producerInfo.implementation = "KeywordClassificationParser";
        auto &audio = input.inferenceInfo.audio;
        audio.sourceId = "commands/pipelineAudioData";
        audio.sessionId = "test-session";
        audio.segmentIndex = 1;
        audio.sampleRateHz = 16000;
        audio.hopSampleCount = 256;
        audio.windowEndSample = 512;
        audio.windowStartTimestampNs = 0;
        audio.windowEndTimestampNs = 32'000'000;
        audio.discontinuity = true;
    }
};

TEST(KeywordClassificationParser, UsesArbitraryLabelsAndTheSelectedOutput) {
    KeywordFixture fixture;
    const std::array<const char *, 4> labels{"open", nullptr, "close", "hold"};
    for (size_t winner = 0; winner < fixture.scores.size(); ++winner) {
        fixture.scores.fill(1000);
        fixture.scores[winner] = 1010;
        Results results;
        ASSERT_TRUE(fixture.parser.parse(fixture.input, results));
        EXPECT_EQ(results.size(), 1u);
        results.for_each<KeywordPayload>([&](const auto &payload) {
            EXPECT_EQ(payload.layer->model, "synthetic-command-scores");
            EXPECT_EQ(payload.layer->infer_element_id, "commands");
            ASSERT_TRUE(payload.layer->producer);
            EXPECT_EQ(payload.layer->producer->instance_id, "commands/post");
            EXPECT_EQ(payload.layer->producer->implementation, "KeywordClassificationParser");
            ASSERT_EQ(payload.classifications.size(), 1u);
            EXPECT_EQ(payload.classifications[0]->kind,
                      open_perception_kit::metadata::ClassificationKind::Categorical);
            EXPECT_EQ(payload.classifications[0]->label_set, "test:commands-v1");
            EXPECT_FALSE(payload.classifications[0]->subject_id);
            EXPECT_EQ(payload.classifications[0]->object->parent_id, 29u);
            ASSERT_TRUE(payload.classifications[0]->audio_window);
            EXPECT_EQ(payload.classifications[0]->audio_window->source_id,
                      "commands/pipelineAudioData");
            if (winner == 1) {
                EXPECT_TRUE(payload.classifications[0]->candidates.empty());
                return;
            }
            ASSERT_EQ(payload.classifications[0]->candidates.size(), 1u);
            const auto &candidate = *payload.classifications[0]->candidates[0];
            EXPECT_EQ(candidate.class_id, winner);
            EXPECT_EQ(candidate.text, labels[winner]);
            EXPECT_NEAR(
                candidate.confidence.value_or(-1), 1.0 / (1.0 + 3.0 * std::exp(-10.0)), 1e-6);
        });
    }
}

TEST(KeywordClassificationParser, KeepsIgnoredClassesInTheCompetitionAndNormalization) {
    KeywordFixture fixture;
    fixture.attributes.set("confidenceThreshold", 0.3);
    Results belowThreshold;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, belowThreshold));
    ASSERT_EQ(belowThreshold.size(), 1u); // All four classes share probability 0.25.
    fixture.attributes.set("confidenceThreshold", 0.25);
    Results tied;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, tied));
    ASSERT_EQ(tied.size(), 1u);
    tied.for_each<KeywordPayload>([](const auto &payload) {
        EXPECT_EQ(payload.classifications[0]->candidates[0]->class_id, 0);
        EXPECT_FLOAT_EQ(payload.classifications[0]->candidates[0]->confidence.value_or(-1), 0.25f);
    });
    fixture.attributes.set("confidenceThreshold", 0.0);
    fixture.scores = {0, 5, 4, 0};
    Results ignoredWinner;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, ignoredWinner));
    ASSERT_EQ(ignoredWinner.size(), 1u);
    fixture.attributes.setArray("labels", {nullptr, "open", "close", "hold"});
    fixture.scores.fill(0);
    Results ignoredTie;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, ignoredTie));
    ASSERT_EQ(ignoredTie.size(), 1u);
    for (const auto *const results : {&belowThreshold, &ignoredWinner, &ignoredTie}) {
        results->for_each<KeywordPayload>([](const auto &payload) {
            ASSERT_EQ(payload.classifications.size(), 1u);
            EXPECT_TRUE(payload.classifications[0]->candidates.empty());
            ASSERT_TRUE(payload.classifications[0]->audio_window);
            EXPECT_TRUE(payload.classifications[0]->audio_window->discontinuity);
        });
    }
}

TEST(KeywordClassificationParser, RoundTripsAnalyzedWindowsIncludingEmptyAndResetAssessments) {
    KeywordFixture fixture;
    fixture.attributes.set("activation", "none");
    Results results;
    fixture.scores = {.9f, .05f, .03f, .02f};
    ASSERT_TRUE(fixture.parser.parse(fixture.input, results));
    auto &audio = fixture.input.inferenceInfo.audio;
    audio.windowStartSample = 256;
    audio.windowEndSample = 768;
    audio.windowStartTimestampNs.reset();
    audio.windowEndTimestampNs.reset();
    audio.discontinuity = false;
    fixture.scores.fill(.25f);
    ASSERT_TRUE(fixture.parser.parse(fixture.input, results));
    audio.segmentIndex = 2;
    audio.windowStartSample = 0;
    audio.windowEndSample = 512;
    audio.discontinuity = true;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, results));
    const auto bytes = results.serialize();
    const Results decoded{std::span<const uint8_t>(bytes)};
    size_t count = 0;
    decoded.for_each<KeywordPayload>([&](const auto &payload) {
        ASSERT_EQ(payload.classifications.size(), 1u);
        const auto &assessment = *payload.classifications[0];
        ASSERT_TRUE(assessment.audio_window);
        const auto &window = *assessment.audio_window;
        EXPECT_EQ(window.source_id, "commands/pipelineAudioData");
        EXPECT_EQ(window.session_id, "test-session");
        EXPECT_EQ(window.sample_rate_hz, 16000u);
        EXPECT_EQ(window.segment_index, count == 2 ? 2u : 1u);
        EXPECT_EQ(window.window_start_sample, count == 1 ? 256u : 0u);
        EXPECT_EQ(window.window_end_sample, count == 1 ? 768u : 512u);
        EXPECT_EQ(window.discontinuity, count != 1);
        EXPECT_EQ(assessment.candidates.size(), count == 0 ? 1u : 0u);
        EXPECT_EQ(window.window_start_ts_ns.has_value(), count == 0);
        if (count == 0) {
            EXPECT_EQ(window.window_start_ts_ns, 0u);
            EXPECT_EQ(window.window_end_ts_ns, 32'000'000u);
        } else {
            EXPECT_FALSE(window.window_end_ts_ns);
        }
        ++count;
    });
    EXPECT_EQ(count, 3u);
}

TEST(KeywordClassificationParser, RejectsMissingOrInvalidWindowWithoutPublishingAnEmptySuccess) {
    KeywordFixture fixture;
    const auto valid = fixture.input.inferenceInfo.audio;
    std::array<opk::AudioInferenceMetadata, 8> invalid;
    invalid.fill(valid);
    invalid[0].sourceId.clear();
    invalid[1].sessionId.clear();
    invalid[2].segmentIndex = 0;
    invalid[3].sampleRateHz = 0;
    invalid[4].windowEndSample = 0;
    invalid[5].windowEndTimestampNs.reset();
    invalid[6].windowStartTimestampNs.reset();
    invalid[7].windowEndTimestampNs = 0;
    for (const auto &audio : invalid) {
        fixture.input.inferenceInfo.audio = audio;
        Results results;
        EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
        EXPECT_EQ(results.size(), 0u);
    }
}

TEST(KeywordClassificationParser, ReadsConfiguredProbabilitiesWithoutRenormalizing) {
    KeywordFixture fixture;
    fixture.attributes.set("activation", "none");
    fixture.attributes.set("confidenceThreshold", 0.7);
    fixture.attributes.setArray("labels", {"activate", "standby", nullptr, "dismiss"});
    fixture.scores = {0.1f, 0.7f, 0.2f, 0.4f};
    Results results;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, results));
    ASSERT_EQ(results.size(), 1u);
    results.for_each<KeywordPayload>([](const auto &payload) {
        const auto &candidate = *payload.classifications[0]->candidates[0];
        EXPECT_EQ(candidate.class_id, 1);
        EXPECT_EQ(candidate.text, "standby");
        EXPECT_FLOAT_EQ(candidate.confidence.value_or(-1), 0.7f);
    });
    fixture.attributes.setArray("labels", {"single-command"});
    fixture.attributes.set("confidenceThreshold", 0.1);
    fixture.tensor = opk::TensorView(
        fixture.scores.data(), sizeof(fixture.scores), opk::Shape(1), opk::Dtype::Float32, 1, 0);
    Results single;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, single));
    EXPECT_EQ(single.size(), 1u);
}

TEST(KeywordClassificationParser, RejectsMissingOrInvalidConfiguration) {
    KeywordFixture fixture;
    const auto complete = fixture.attributes.cloneDeep();
    for (const auto *const name :
         {"outputTensorIndex", "activation", "confidenceThreshold", "labels", "labelSet"}) {
        SCOPED_TRACE(name);
        fixture.attributes = complete.cloneDeep();
        fixture.attributes.raw().erase(name);
        Results results;
        EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
        EXPECT_EQ(results.size(), 0u);
    }
    const auto invalidAttributes = std::to_array<std::pair<const char *, opk::AttributeValue>>(
        {{"outputTensorIndex", -1},
         {"outputTensorIndex", 16},
         {"outputTensorIndex", 1.0},
         {"activation", "sigmoid"},
         {"activation", 1},
         {"confidenceThreshold", -0.1},
         {"confidenceThreshold", 1.1},
         {"confidenceThreshold", "0.6"},
         {"confidenceThreshold", std::numeric_limits<double>::quiet_NaN()},
         {"labels", "open"},
         {"labelSet", ""},
         {"labelSet", 1}});
    for (const auto &[name, value] : invalidAttributes) {
        SCOPED_TRACE(name);
        fixture.attributes = complete.cloneDeep();
        fixture.attributes[name] = value;
        Results results;
        EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
        EXPECT_EQ(results.size(), 0u);
    }
    const std::array<opk::AttributeValue::Array, 5> invalidLabels{
        opk::AttributeValue::Array{},
        opk::AttributeValue::Array{nullptr, nullptr, nullptr, nullptr},
        opk::AttributeValue::Array{"open", "", "close", nullptr},
        opk::AttributeValue::Array{"open", 3, "close", nullptr},
        opk::AttributeValue::Array{"open", "close"}};
    for (const auto &labels : invalidLabels) {
        fixture.attributes = complete.cloneDeep();
        fixture.attributes.setArray("labels", labels);
        Results results;
        EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
        EXPECT_EQ(results.size(), 0u);
    }
}

TEST(KeywordClassificationParser, RejectsInvalidScoresAndTensorShapes) {
    KeywordFixture fixture;
    fixture.attributes.set("activation", "none");
    for (const float score : {-0.1f,
                              1.1f,
                              std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity()}) {
        fixture.scores = {0.9f, score, 0, 0};
        Results results;
        EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
        EXPECT_EQ(results.size(), 0u);
    }
    fixture.scores.fill(0);
    Results results;
    fixture.input.tensors[1] = nullptr;
    EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
    fixture.input.tensors[1] = &fixture.tensor;
    fixture.attributes.setArray("labels", {"first", "second"});
    fixture.tensor = opk::TensorView(
        fixture.scores.data(), sizeof(fixture.scores), opk::Shape(2, 2), opk::Dtype::Float32, 1, 0);
    EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
    fixture.tensor = opk::TensorView(
        fixture.scores.data(), sizeof(float), opk::Shape(4), opk::Dtype::Float32, 1, 0);
    EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
    fixture.tensor = opk::TensorView(
        fixture.scores.data(), sizeof(fixture.scores), opk::Shape(4), opk::Dtype::Int8, 1, 0);
    EXPECT_FALSE(fixture.parser.parse(fixture.input, results));
    EXPECT_EQ(results.size(), 0u);
}

} // namespace
