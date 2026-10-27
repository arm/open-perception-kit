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

#include "open_perception_kit.h"
#include "runtime/Pipeline.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <poll.h>
#include <span>
#include <termios.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using Vad = open_perception_kit::metadata::VoiceActivityDetectionT;
using Turn = open_perception_kit::metadata::TurnDetectionT;
using Results = open_perception_kit::container::envelope;

// All audio perceptions use mono 16 kHz sample positions, not wall-clock time.
constexpr uint64_t kSilenceTimeoutSamples = 16000 * 2500 / 1000;
constexpr unsigned kRequiredPositiveAssessments = 2;
volatile std::sig_atomic_t interrupted = 0;

void interrupt(int) {
    interrupted = 1;
}

struct Conversation {
    bool utteranceOpen = false;
    bool speechActive = false;
    uint64_t lastSpeechEnd = 0;
    std::optional<uint64_t> lastVadEnd;
    std::optional<uint64_t> lastTurnEnd;
    unsigned positiveAssessments = 0;

    void complete(const char *const reason) {
        // Re-arm only after new speech. The model's retries are one turn.
        utteranceOpen = false;
        positiveAssessments = 0;
        // Replace the live status with a permanent event line.
        std::printf("\r\033[2K\033[7mTURN (%s)\033[0m\n", reason);
        std::fflush(stdout);
    }

    void showStatus() const {
        // Redraw one line instead of filling the terminal with every VAD window.
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
        if (vad.window_end_sample <= vad.window_start_sample) {
            *this = {};
            return;
        }
        if (vad.discontinuity || (lastVadEnd && vad.window_start_sample > *lastVadEnd))
            *this = {}; // Missing audio cannot count as continuous silence.
        if (lastVadEnd && vad.window_end_sample <= *lastVadEnd)
            return;

        lastVadEnd = vad.window_end_sample;
        speechActive = vad.speech_active;
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
        if (!utteranceOpen || speechActive || !lastVadEnd ||
            turn.window_end_sample <= turn.window_start_sample ||
            turn.window_end_sample <= lastSpeechEnd || turn.window_end_sample > *lastVadEnd ||
            (lastTurnEnd && turn.window_end_sample <= *lastTurnEnd))
            return;

        // SmartTurn's first assessment can carry a delayed reset marker.
        // Reset its confirmations, while retaining speech already seen by VAD.
        if (turn.discontinuity)
            positiveAssessments = 0;
        lastTurnEnd = turn.window_end_sample;
        positiveAssessments = turn.turn_complete ? positiveAssessments + 1 : 0;
        if (positiveAssessments >= kRequiredPositiveAssessments)
            complete("turn detector decision");
    }

    void consume(const Results &results) {
        // A buffer can carry several windows. Process them in audio order,
        // with VAD before SmartTurn at the same endpoint. The pointers below
        // remain valid while this decoded FrameResults object is alive.
        struct Assessment {
            uint64_t endSample;
            const Vad *vad;
            const Turn *turn;
        };
        std::vector<Assessment> assessments;
        results.for_each<open_perception_kit::metadata::VoiceActivityDetectionsT>(
            [&](const auto &payload) {
                // This demo has one stream: opkinfer name=voice in the pipeline below.
                if (!payload.layer || payload.layer->infer_element_id != "voice")
                    return;
                for (const auto &vad : payload.detections) {
                    if (vad)
                        assessments.push_back({vad->window_end_sample, vad.get(), nullptr});
                }
            });
        results.for_each<open_perception_kit::metadata::TurnDetectionsT>([&](const auto &payload) {
            if (!payload.layer || payload.layer->infer_element_id != "voice")
                return;
            for (const auto &turn : payload.detections) {
                if (turn)
                    assessments.push_back({turn->window_end_sample, nullptr, turn.get()});
            }
        });
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

// Read ESC without Enter or keyboard echo; restore the terminal on every return.
struct Keyboard {
    termios saved{};
    bool enabled = false;

    bool enable() {
        if (tcgetattr(STDIN_FILENO, &saved) != 0)
            return false;
        auto settings = saved;
        settings.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        settings.c_cc[VMIN] = 1;
        settings.c_cc[VTIME] = 0;
        enabled = tcsetattr(STDIN_FILENO, TCSANOW, &settings) == 0;
        return enabled;
    }

    ~Keyboard() {
        if (enabled)
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
};

} // namespace

int main() {
    Keyboard keyboard;
    if (!keyboard.enable()) {
        std::perror("voice-pipeline requires an interactive terminal");
        return 1;
    }
    std::signal(SIGINT, interrupt);
    std::signal(SIGTERM, interrupt);

    // Read and copy environment values before loading plugins or starting worker threads.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *const rootEnv = std::getenv("OPK_PROJECT_ROOT");
    const std::filesystem::path root = rootEnv && *rootEnv ? rootEnv : "/work";
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *const pluginEnv = std::getenv("OPK_PLUGIN_PATH");
    const std::filesystem::path plugins =
        pluginEnv && *pluginEnv ? pluginEnv : root / "development/build-active/meson-out";
    const auto pluginResult = opk::runtime::Pipeline::addPluginPath(plugins.string());
    if (!pluginResult) {
        std::fprintf(stderr, "%s\n", pluginResult.error().toString().c_str());
        return 1;
    }

    // These objects outlive Pipeline and its streaming callbacks.
    Conversation conversation;
    std::mutex conversationMutex;
    std::atomic_bool done{false};
    std::atomic_bool failed{false};

    // Black video supplies the web viewer; browser microphone audio runs through
    // the combined VAD/SmartTurn chain. Pipeline expands the project-root variable.
    auto loaded = opk::runtime::Pipeline::fromString(R"(
        videotestsrc is-live=true pattern=black !
        video/x-raw,format=BGRA,width=640,height=480,framerate=30/1 !
        opksink name=sink
        opksrc ws-port=8003 ! queue !
        opkinfer name=voice
            opchain-path="${OPK_PROJECT_ROOT:-/work}/config/models/smart-turn/opchain-vad.json"
            audio-hop-samples=512 active=true !
        sink.audiosink
    )");
    if (!loaded) {
        std::fprintf(stderr, "%s\n", loaded.error().toString().c_str());
        return 1;
    }

    auto pipeline = std::move(*loaded);

    // Handle pipeline events

    pipeline.onFrameResultsPacket([&](const std::vector<uint8_t> &packet) {
        const Results results{std::span<const uint8_t>{packet}};
        if (!results.valid() ||
            results.producer_identity() !=
                open_perception_kit::container::producer_identity_status::exact_match) {
            std::fprintf(stderr, "Invalid or incompatible FrameResults packet\n");
            failed = true;
            done = true;
            return;
        }
        // Pipeline callbacks may run on different streaming threads.
        const std::scoped_lock lock(conversationMutex);
        conversation.consume(results);
    });

    pipeline.onError([&](const opk::runtime::Error &error) {
        std::fprintf(stderr, "%s\n", error.toString().c_str());
        failed = true;
        done = true;
    });

    pipeline.onEos([&]() { done = true; });

    // Info
    std::puts("\033[31;7mVOICE DEMO: detects speech and when your turn ends.\033[0m");
    std::puts("\033[7m\033[36mbrowser microphone -> opksrc -> Silero VAD -> SmartTurn -> "
              "opksink\033[0m");
    std::puts("Use the OPK web frontend for microphone input!");
    std::fflush(stdout);

    // Start the pipeline
    const auto started = pipeline.start();
    if (!started) {
        std::fprintf(stderr, "%s\n", started.error().toString().c_str());
        return 1;
    }

    // Run until ESC is pressed
    while (!done && !interrupted) {
        {
            // The callback also prints turn events under this mutex.
            const std::scoped_lock lock(conversationMutex);
            conversation.showStatus();
        }
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const int ready = poll(&input, 1, 100);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            std::perror("poll");
            failed = true;
            break;
        }
        if (input.revents & POLLIN) {
            char key = 0;
            if (read(STDIN_FILENO, &key, 1) != 1 || key == '\033')
                break;
        }
        if (input.revents & (POLLHUP | POLLERR | POLLNVAL))
            break;
    }

    // Stop on the main thread, after ESC/EOS/error; never from a streaming callback.
    const auto stopped = pipeline.stop();
    std::printf("\r\033[2K");
    std::fflush(stdout);

    if (!stopped) {
        std::fprintf(stderr, "%s\n", stopped.error().toString().c_str());
        return 1;
    }
    return failed ? 1 : 0;
}
