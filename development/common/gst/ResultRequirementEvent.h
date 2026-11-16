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

#include "opk/OutputCapability.h"
#include <gst/gst.h>
#include <optional>

namespace opk::result_requirement_event {

/**
 * GstStructure name for the custom upstream event emitted when an active
 * inference element requires content from an upstream inference element.
 * Matching upstream producers activate themselves and continue forwarding the
 * event so that every matching producer can be enabled. The event never
 * disables elements.
 */
inline constexpr const char *k_name = "opk-result-required";

inline GstStructure *structure(const char *name, const OutputCapability &output) {
    return gst_structure_new(name,
                             "payload-type",
                             G_TYPE_STRING,
                             output.payloadType.c_str(),
                             "role",
                             G_TYPE_STRING,
                             output.role.c_str(),
                             nullptr);
}
inline std::optional<OutputCapability> read(const GstStructure *const value) {
    if (!value)
        return std::nullopt;
    const auto *const type = gst_structure_get_string(value, "payload-type");
    const auto *const role = gst_structure_get_string(value, "role");
    if (!type || !*type || !role)
        return std::nullopt;
    return OutputCapability{type, role};
}
inline void writeList(GstStructure *const target,
                      const char *field,
                      const std::vector<OutputCapability> &outputs) {
    GValue list = G_VALUE_INIT;
    g_value_init(&list, GST_TYPE_LIST);
    for (const auto &output : outputs) {
        GstStructure *const entry = structure("opk-output", output);
        GValue value = G_VALUE_INIT;
        g_value_init(&value, GST_TYPE_STRUCTURE);
        gst_value_set_structure(&value, entry);
        gst_value_list_append_value(&list, &value);
        g_value_unset(&value);
        gst_structure_free(entry);
    }
    gst_structure_set_value(target, field, &list);
    g_value_unset(&list);
}
inline std::vector<OutputCapability> readList(const GstStructure *const source, const char *field) {
    std::vector<OutputCapability> outputs;
    const auto *const list = gst_structure_get_value(source, field);
    if (!list || !GST_VALUE_HOLDS_LIST(list))
        return outputs;
    for (guint index = 0; index < gst_value_list_get_size(list); ++index) {
        const auto *const entry = gst_value_list_get_value(list, index);
        if (!GST_VALUE_HOLDS_STRUCTURE(entry))
            return {};
        const auto output = read(gst_value_get_structure(entry));
        if (!output)
            return {};
        outputs.push_back(*output);
    }
    return outputs;
}

} // namespace opk::result_requirement_event
