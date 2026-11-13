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
        payload.layer->content_type = "keyword";
        auto classification = std::make_unique<open_perception_kit::metadata::ClassificationT>();
        classification->object = std::make_unique<open_perception_kit::metadata::ObjectMetaT>();
        auto candidate =
            std::make_unique<open_perception_kit::metadata::ClassificationCandidateT>();
        candidate->text = label;
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
