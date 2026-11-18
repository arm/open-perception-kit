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

#include "gst/PromptInputsMeta.h"

#include <gst/video/video.h>
#include <new>
#include <utility>

namespace opk {
namespace {

gboolean initialize(GstMeta *const meta, gpointer, GstBuffer *) {
    auto *const prompt = reinterpret_cast<PromptInputsMeta *>(meta);
    new (&prompt->snapshot) std::shared_ptr<const PromptSnapshot>();
    new (&prompt->targetElement) std::string();
    prompt->width = 0;
    prompt->height = 0;
    return TRUE;
}

void release(GstMeta *const meta, GstBuffer *) {
    auto *const prompt = reinterpret_cast<PromptInputsMeta *>(meta);
    std::destroy_at(&prompt->snapshot);
    std::destroy_at(&prompt->targetElement);
}

gboolean transform(GstBuffer *const destination,
                   GstMeta *const meta,
                   GstBuffer *,
                   const GQuark type,
                   gpointer data) {
    // Only a whole-buffer copy preserves the qualified image geometry.
    if (!GST_META_TRANSFORM_IS_COPY(type) ||
        (data && static_cast<const GstMetaTransformCopy *>(data)->region)) {
        GST_WARNING("PromptInputsMeta rejects unsupported geometry/region transformation");
        return FALSE;
    }
    const auto *const source = reinterpret_cast<const PromptInputsMeta *>(meta);
    return PromptInputsMeta::add(destination,
                                 source->snapshot,
                                 source->targetElement,
                                 source->width,
                                 source->height) != nullptr;
}

} // namespace

GType PromptInputsMeta::apiType() {
    static gsize type = 0;
    if (g_once_init_enter(&type)) {
        static const gchar *tags[] = {GST_META_TAG_VIDEO_STR, nullptr};
        const GType registered = gst_meta_api_type_register("OpkPromptInputsMetaAPI_v1", tags);
        g_once_init_leave(&type, registered);
    }
    return static_cast<GType>(type);
}

const GstMetaInfo *PromptInputsMeta::info() {
    static const GstMetaInfo *registered = nullptr;
    if (g_once_init_enter_pointer(&registered)) {
        const GstMetaInfo *const value = gst_meta_register(apiType(),
                                                           "OpkPromptInputsMeta_v1",
                                                           sizeof(PromptInputsMeta),
                                                           initialize,
                                                           release,
                                                           transform);
        g_once_init_leave_pointer(&registered, const_cast<GstMetaInfo *>(value));
    }
    return registered;
}

PromptInputsMeta *PromptInputsMeta::add(GstBuffer *const buffer,
                                        std::shared_ptr<const PromptSnapshot> snapshot,
                                        const std::string &targetElement,
                                        const guint width,
                                        const guint height) {
    if (!buffer || !gst_buffer_is_writable(buffer) || !snapshot || get(buffer)) {
        return nullptr;
    }
    auto *const result =
        reinterpret_cast<PromptInputsMeta *>(gst_buffer_add_meta(buffer, info(), nullptr));
    if (result) {
        result->snapshot = std::move(snapshot);
        result->targetElement = targetElement;
        result->width = width;
        result->height = height;
    }
    return result;
}

const PromptInputsMeta *PromptInputsMeta::get(GstBuffer *const buffer) {
    return reinterpret_cast<const PromptInputsMeta *>(gst_buffer_get_meta(buffer, apiType()));
}

} // namespace opk
