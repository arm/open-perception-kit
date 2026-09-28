/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "opk/Result.h"

#include <optional>
#include <string>

namespace opk {

/**
 * @brief Parsed top-level OPK pipeline preset.
 *
 * The version, description, and pipeline are validated before parsing. Optional
 * source information and loop metadata are retained so launchers do not need to
 * parse the JSON again.
 */
struct PipelinePreset {
    /// Human-readable text required by pipeline presets and shown by launchers.
    std::optional<std::string> description;

    /// Short source requirement shown by launchers such as opk-menu.
    std::optional<std::string> sourceInfo;

    /// GStreamer launch-syntax pipeline description.
    std::string pipeline;

    /// Whether a launcher should repeat the pipeline after end-of-stream.
    bool loop = false;

    /**
     * @brief Parses a pipeline preset from JSON text.
     * @param jsonText Pipeline preset JSON.
     * @param source Source name used in diagnostics.
     * @return Parsed preset or a validation/parse error.
     */
    static Result<PipelinePreset> fromJson(const std::string &jsonText,
                                           const std::string &source = "pipeline.json");

    /**
     * @brief Loads and parses a pipeline preset JSON file.
     * @param path Path to the preset.
     * @return Parsed preset or a file/validation/parse error.
     */
    static Result<PipelinePreset> fromFile(const std::string &path);
};

/**
 * @brief Expands environment placeholders in a GStreamer pipeline description.
 *
 * Supported forms are `${VAR}`, `${VAR:-default}`, and `${VAR?error message}`.
 * An unset `${VAR}` becomes empty. The default form uses its fallback for an
 * unset or empty variable, while the required form returns an error.
 *
 * @param description Pipeline description containing optional placeholders.
 * @return Expanded description or a placeholder parse/validation error.
 */
Result<std::string> expandPipelineDescription(const std::string &description);

} // namespace opk
