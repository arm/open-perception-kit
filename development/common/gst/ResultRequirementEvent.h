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
 * inference element requires one concrete upstream output. Compatibility uses
 * the payload type; addressing uses the producer instance ID. No semantic roles
 * or wildcard producer selection are supported. The event never disables elements.
 */
inline constexpr const char *k_name = "opk-result-required";

inline GstStructure *structure(const char *name, const OutputReference &output) {
    return gst_structure_new(name,
                             "payload-type",
                             G_TYPE_STRING,
                             output.payloadType.c_str(),
                             "producer-id",
                             G_TYPE_STRING,
                             output.producerId.c_str(),
                             nullptr);
}
inline std::optional<OutputReference> read(const GstStructure *const value) {
    if (!value)
        return std::nullopt;
    const auto *const type = gst_structure_get_string(value, "payload-type");
    const auto *const producer = gst_structure_get_string(value, "producer-id");
    if (!type || !*type || !producer)
        return std::nullopt;
    return OutputReference{type, producer};
}
template <typename Entry>
inline void
writeList(GstStructure *const target, const char *field, const std::vector<Entry> &outputs) {
    GValue list = G_VALUE_INIT;
    g_value_init(&list, GST_TYPE_LIST);
    for (const auto &output : outputs) {
        GstStructure *const entry =
            structure("opk-output", {output.payloadType, output.producerId});
        if constexpr (requires { output.consumerId; })
            gst_structure_set(entry,
                              "consumer-id",
                              G_TYPE_STRING,
                              output.consumerId.c_str(),
                              "input-id",
                              G_TYPE_STRING,
                              output.inputId.c_str(),
                              nullptr);
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
inline std::vector<OutputReference> readOutputs(const GstStructure *const source,
                                                const char *field) {
    std::vector<OutputReference> outputs;
    const auto *const list = gst_structure_get_value(source, field);
    if (!list || !GST_VALUE_HOLDS_LIST(list))
        return outputs;
    for (guint index = 0; index < gst_value_list_get_size(list); ++index) {
        const auto *const entry = gst_value_list_get_value(list, index);
        if (!GST_VALUE_HOLDS_STRUCTURE(entry))
            return {};
        const auto output = read(gst_value_get_structure(entry));
        if (!output || output->producerId.empty())
            return {};
        outputs.push_back(*output);
    }
    return outputs;
}

inline std::vector<InputBinding> readInputs(const GstStructure *const source, const char *field) {
    std::vector<InputBinding> inputs;
    const auto *const list = gst_structure_get_value(source, field);
    if (!list || !GST_VALUE_HOLDS_LIST(list))
        return inputs;
    for (guint index = 0; index < gst_value_list_get_size(list); ++index) {
        const auto *const entry = gst_value_list_get_value(list, index);
        if (!GST_VALUE_HOLDS_STRUCTURE(entry))
            return {};
        const auto *const value = gst_value_get_structure(entry);
        const auto input = read(value);
        const auto *const consumer = gst_structure_get_string(value, "consumer-id");
        const auto *const inputId = gst_structure_get_string(value, "input-id");
        if (!input || !consumer || !*consumer || !inputId || !*inputId)
            return {};
        inputs.push_back({consumer, inputId, input->payloadType, input->producerId});
    }
    return inputs;
}

} // namespace opk::result_requirement_event
