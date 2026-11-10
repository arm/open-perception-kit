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
#include "../examples/voice-pipeline/Conversation.h"
#include <gtest/gtest.h>
using namespace voice_example;

TEST(VoiceConversation, FalseIsNotMissingAndUnknownAssessmentsCannotCompleteATurn) {
    Conversation conversation;
    Vad vad;
    vad.speech_probability = 1;
    vad.speech_active = true;
    vad.window_end_sample = 512;
    conversation.onVad(vad);
    EXPECT_TRUE(conversation.utteranceOpen);
    vad.window_start_sample = 512;
    vad.window_end_sample = 1024;
    vad.speech_probability = 0;
    vad.speech_active = false;
    conversation.onVad(vad);
    EXPECT_FALSE(conversation.speechActive);
    Turn turn;
    turn.window_end_sample = 1024;
    turn.turn_probability = 0;
    turn.turn_complete = false;
    conversation.onTurn(turn);
    EXPECT_EQ(conversation.positiveAssessments, 0U);
    EXPECT_TRUE(conversation.utteranceOpen);
    for (uint64_t endpoint : {1536U, 2048U}) {
        vad.window_start_sample = endpoint - 512;
        vad.window_end_sample = endpoint;
        conversation.onVad(vad);
        turn.window_end_sample = endpoint;
        turn.turn_probability = 1;
        turn.turn_complete = true;
        conversation.onTurn(turn);
    }
    EXPECT_FALSE(conversation.utteranceOpen);
    vad.speech_active = flatbuffers::nullopt;
    conversation.onVad(vad);
    EXPECT_FALSE(conversation.lastVadEnd);
}

TEST(VoiceConversation, MultipleProducersForOneWindowAreAmbiguous) {
    Conversation conversation;
    Results result;
    for (const auto *id : {"one", "two"}) {
        open_perception_kit::metadata::VoiceActivityDetectionsT payload;
        payload.layer = std::make_unique<open_perception_kit::metadata::LayerInfoT>();
        payload.layer->infer_element_id = "voice";
        payload.layer->producer = std::make_unique<open_perception_kit::metadata::ProducerInfoT>();
        payload.layer->producer->instance_id = id;
        payload.layer->producer->component = "test";
        auto value = std::make_unique<Vad>();
        value->speech_probability = 1;
        value->speech_active = true;
        value->window_end_sample = 512;
        payload.detections.push_back(std::move(value));
        result.add(std::move(payload));
    }
    conversation.consume(result);
    EXPECT_FALSE(conversation.utteranceOpen);
    EXPECT_FALSE(conversation.lastVadEnd);
}
