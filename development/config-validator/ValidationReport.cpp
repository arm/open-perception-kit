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

#include "ValidatorInternal.h"

#include <glib.h>

#include <algorithm>
#include <iterator>
#include <sstream>
#include <tuple>

namespace opk::config {
namespace {

std::string_view phaseName(ValidationPhase phase) {
    using enum ValidationPhase;
    switch (phase) {
    case Parse:
        return "parse";
    case Dispatch:
        return "dispatch";
    case Schema:
        return "schema";
    case Descriptor:
        return "descriptor";
    }
    return "descriptor";
}

} // namespace
namespace detail {

ValidationIssue makeIssue(std::string rule,
                          ValidationPhase phase,
                          std::string_view file,
                          std::string instanceLocation,
                          std::string message,
                          std::optional<std::string> related) {
    return ValidationIssue{std::move(rule),
                           phase,
                           std::string(file),
                           std::move(instanceLocation),
                           std::move(related),
                           std::move(message)};
}

void append(ValidationReport &target, ValidationReport source) {
    target.issues.insert(target.issues.end(),
                         std::make_move_iterator(source.issues.begin()),
                         std::make_move_iterator(source.issues.end()));
}

void validateControlFreeString(ValidationReport &report,
                               std::string_view value,
                               std::string_view source,
                               std::string_view instanceLocation,
                               std::string_view rule) {
    if (value.empty())
        return;

    const gchar *current = value.data();
    const gchar *end = current + value.size();
    while (current < end) {
        const gunichar codepoint =
            *current == '\0' ? 0 : g_utf8_get_char_validated(current, end - current);
        if (codepoint == static_cast<gunichar>(-1) || codepoint == static_cast<gunichar>(-2))
            return;
        if (codepoint <= 0x1f || (codepoint >= 0x7f && codepoint <= 0x9f)) {
            report.issues.push_back(makeIssue(std::string(rule),
                                              ValidationPhase::Descriptor,
                                              source,
                                              std::string(instanceLocation),
                                              "value must not contain control characters"));
            return;
        }
        current = g_utf8_next_char(current);
    }
}

} // namespace detail

void ValidationReport::sort() {
    std::ranges::sort(issues, [](const auto &left, const auto &right) {
        return std::tie(left.file, left.phase, left.instanceLocation, left.rule, left.message) <
               std::tie(right.file, right.phase, right.instanceLocation, right.rule, right.message);
    });
}

std::string ValidationIssue::toText() const {
    std::ostringstream output;
    output << file;
    if (!instanceLocation.empty())
        output << ':' << instanceLocation;
    output << ": " << phaseName(phase) << ' ' << rule << ": " << message;
    return output.str();
}

std::string ValidationReport::toText() const {
    if (issues.empty())
        return "Configuration descriptors are valid.\n";

    std::ostringstream output;
    for (const auto &issue : issues) {
        output << issue.toText() << '\n';
    }
    return output.str();
}

} // namespace opk::config
