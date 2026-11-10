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
#include "opk/AudioResults.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace voice_example {
using Vad = open_perception_kit::metadata::VoiceActivityDetectionT;
using Turn = open_perception_kit::metadata::TurnDetectionT;
using Results = open_perception_kit::container::envelope;

// Model context is mono 16 kHz sample positions, not wall-clock time.
inline constexpr uint64_t kSilenceTimeoutSamples = uint64_t{16000} * 2500 / 1000;
inline constexpr unsigned kRequiredPositiveAssessments = 2;

struct Conversation {
    bool utteranceOpen = false;
    bool speechActive = false;
    uint64_t lastSpeechEnd = 0;
    std::optional<uint64_t> lastVadEnd;
    std::optional<uint64_t> lastTurnEnd;
    unsigned positiveAssessments = 0;

    void complete(const char *const reason) {
        utteranceOpen = false;
        positiveAssessments = 0;
        std::printf("\r\033[2K\033[7mTURN (%s)\033[0m\n", reason);
        std::fflush(stdout);
    }

    void showStatus() const {
        std::printf("\r\033[2K\033[7m ESC \033[0m - Quit | ");
        if (!lastVadEnd) {
            std::printf("Waiting for audio...");
        } else {
            std::printf("Audio: %.1f s | ", static_cast<double>(*lastVadEnd) / 16000.0);
            if (speechActive)
                std::printf("Voice detected");
            else if (utteranceOpen)
                std::printf("Silence: %.1f s | Turn confirmation: %u/%u",
                            static_cast<double>(*lastVadEnd - lastSpeechEnd) / 16000.0,
                            positiveAssessments,
                            kRequiredPositiveAssessments);
            else
                std::printf("Listening for speech...");
        }
        std::fflush(stdout);
    }

    void onVad(const Vad &vad) {
        if (!vad.speech_active || !opk::audio_results::valid(vad)) {
            *this = {};
            return;
        }
        if (vad.discontinuity || (lastVadEnd && vad.window_start_sample > *lastVadEnd))
            *this = {}; // Missing audio cannot count as continuous silence.
        if (lastVadEnd && vad.window_end_sample <= *lastVadEnd)
            return;
        lastVadEnd = vad.window_end_sample;
        speechActive = *vad.speech_active;
        if (speechActive) {
            utteranceOpen = true;
            lastSpeechEnd = vad.window_end_sample;
            positiveAssessments = 0;
            lastTurnEnd.reset();
        } else if (utteranceOpen &&
                   vad.window_end_sample - lastSpeechEnd >= kSilenceTimeoutSamples) {
            complete("silence timeout");
        }
    }

    void onTurn(const Turn &turn) {
        if (!turn.turn_complete || !opk::audio_results::valid(turn)) {
            positiveAssessments = 0;
            return;
        }
        if (!utteranceOpen || speechActive || !lastVadEnd ||
            turn.window_end_sample <= turn.window_start_sample ||
            turn.window_end_sample <= lastSpeechEnd || turn.window_end_sample > *lastVadEnd ||
            (lastTurnEnd && turn.window_end_sample <= *lastTurnEnd))
            return;
        // A delayed SmartTurn reset clears confirmations, not speech already seen by VAD.
        if (turn.discontinuity)
            positiveAssessments = 0;
        lastTurnEnd = turn.window_end_sample;
        positiveAssessments = *turn.turn_complete ? positiveAssessments + 1 : 0;
        if (positiveAssessments >= kRequiredPositiveAssessments)
            complete("turn detector decision");
    }

    void consume(const Results &results) {
        // Process all windows chronologically, VAD before a turn assessment at the same endpoint.
        struct Assessment {
            uint64_t endSample;
            const Vad *vad;
            const Turn *turn;
        };
        std::vector<Assessment> assessments;
        std::set<std::pair<std::string, std::string>> vadSources, turnSources;
        bool invalidSource = false;
        const auto recordSource = [&](const auto &payload, auto &sources) {
            if (payload.detections.empty())
                return;
            if (!payload.layer->producer || payload.layer->producer->instance_id.empty() ||
                payload.layer->producer->component.empty()) {
                invalidSource = true;
                return;
            }
            sources.emplace(payload.layer->producer->component,
                            payload.layer->producer->instance_id);
        };
        results.for_each<open_perception_kit::metadata::VoiceActivityDetectionsT>(
            [&](const auto &payload) {
                // This demo has one stream: opkinfer name=voice.
                if (!payload.layer || payload.layer->infer_element_id != "voice")
                    return;
                recordSource(payload, vadSources);
                for (const auto &vad : payload.detections)
                    if (vad)
                        assessments.push_back({vad->window_end_sample, vad.get(), nullptr});
            });
        results.for_each<open_perception_kit::metadata::TurnDetectionsT>([&](const auto &payload) {
            if (!payload.layer || payload.layer->infer_element_id != "voice")
                return;
            recordSource(payload, turnSources);
            for (const auto &turn : payload.detections)
                if (turn)
                    assessments.push_back({turn->window_end_sample, nullptr, turn.get()});
        });
        if (invalidSource || vadSources.size() > 1 || turnSources.size() > 1) {
            *this = {};
            return;
        }
        std::map<std::pair<uint64_t, bool>, size_t> windows;
        for (const auto &assessment : assessments)
            if (++windows[{assessment.endSample, assessment.vad != nullptr}] > 1) {
                *this = {};
                return;
            }
        std::stable_sort(assessments.begin(), assessments.end(), [](const auto &a, const auto &b) {
            if (a.endSample != b.endSample)
                return a.endSample < b.endSample;
            return a.vad != nullptr && b.vad == nullptr;
        });
        for (const auto &assessment : assessments) {
            if (assessment.vad)
                onVad(*assessment.vad);
            else
                onTurn(*assessment.turn);
        }
    }
};
} // namespace voice_example
