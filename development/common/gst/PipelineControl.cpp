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

#include "PipelineControl.h"

#include <memory>
#include <string>
#include <utility>

namespace gst::pipeline_control {
namespace {

constexpr const char *kPendingLink = "opk-pending-dynamic-pad-link";

struct PendingPadLink {
    std::shared_ptr<GstPad> m_previousSource;
    std::shared_ptr<GstPad> m_destination;
    std::string m_streamId;
    gulong m_handlerId = 0;
};

void reconnectPad(GstElement *const source, GstPad *const pad, gpointer const data) {
    const auto *const link = static_cast<PendingPadLink *>(data);
    // A pad can have only one connection, so if the new output pad or the saved input pad is
    // already connected then do nothing.
    if (gst_pad_is_linked(pad) || gst_pad_is_linked(link->m_destination.get())) {
        return;
    }
    g_autofree gchar *streamId = gst_pad_get_stream_id(pad);
    const bool streamIsDifferent =
        !link->m_streamId.empty() && streamId && link->m_streamId != streamId;
    if (streamIsDifferent) {
        return;
    }
    if (gst_pad_link(pad, link->m_destination.get()) == GST_PAD_LINK_OK) {
        g_signal_handler_disconnect(source, link->m_handlerId);
    }
}

void releasePendingLink(gpointer const data, GClosure *const /*closure needed by gst signature*/) {
    const std::unique_ptr<PendingPadLink> link(static_cast<PendingPadLink *>(data));
    g_object_set_data(G_OBJECT(link->m_previousSource.get()), kPendingLink, nullptr);
}

void rememberPadLink(const GValue *const item, gpointer const /*data needed by gst signature*/) {
    auto *const pad = GST_PAD(g_value_get_object(item));
    g_autoptr(GstPadTemplate) padTemplate = gst_pad_get_pad_template(pad);
    const bool isDynamicSource =
        padTemplate && GST_PAD_TEMPLATE_PRESENCE(padTemplate) == GST_PAD_SOMETIMES;
    const bool callbackIsAlreadyRegistered = g_object_get_data(G_OBJECT(pad), kPendingLink);
    if (!isDynamicSource || callbackIsAlreadyRegistered) {
        return;
    }
    std::shared_ptr<GstPad> destination(gst_pad_get_peer(pad), gst_object_unref);
    if (!destination) {
        return;
    }

    g_autoptr(GstElement) source = gst_pad_get_parent_element(pad);
    GST_OBJECT_LOCK(source);
    const bool needsStreamIdentity = source->numsrcpads > 1;
    GST_OBJECT_UNLOCK(source);
    g_autofree gchar *streamId = gst_pad_get_stream_id(pad);
    // Single-output sources can generate a new stream ID on every start.
    auto pendingLink = std::make_unique<PendingPadLink>(
        std::shared_ptr<GstPad>(GST_PAD(gst_object_ref(pad)), gst_object_unref),
        std::move(destination),
        needsStreamIdentity && streamId ? streamId : "");

    // Mark that a callback is already registered to this connection
    // It is kept before the actual registration because the releasePendingLink can be called
    // asynchronously once it is registered and it cleans up the pendingLink data
    g_object_set_data(G_OBJECT(pad), kPendingLink, pendingLink.get());

    // Register reconnect function and save handler ID so it can be deregistered once it is used.
    // The signal closure owns the saved connection until relink or disposal.
    auto *const callbackData = pendingLink.get();
    callbackData->m_handlerId = g_signal_connect_data(
        source,
        "pad-added",
        G_CALLBACK(reconnectPad),
        pendingLink.release(), // Stores the raw pointer as callback data
        releasePendingLink,    // Register cleanup function for PendingPadLink pointer
        GConnectFlags{});
}

bool exposesDynamicSources(GstElement *const element) {
    const auto *padTemplate =
        gst_element_class_get_pad_template_list(GST_ELEMENT_GET_CLASS(element));
    for (; padTemplate; padTemplate = padTemplate->next) {
        const auto *const candidate = GST_PAD_TEMPLATE(padTemplate->data);
        if (GST_PAD_TEMPLATE_DIRECTION(candidate) == GST_PAD_SRC &&
            GST_PAD_TEMPLATE_PRESENCE(candidate) == GST_PAD_SOMETIMES) {
            return true;
        }
    }
    return false;
}

// Stopping to READY or NULL can remove dynamic pads; gst_parse_launch links them only once.
// Remember their destinations so recreated pads can be reconnected on restart.
void rememberDynamicLinks(GstElement *const element) {
    const bool ownsDynamicSources = exposesDynamicSources(element);
    if (!ownsDynamicSources && !GST_IS_BIN(element)) {
        return;
    }
    // Decoder bins own their internal links; only preserve their exposed connections.
    g_autoptr(GstIterator) iterator = ownsDynamicSources
                                          ? gst_element_iterate_src_pads(element)
                                          : gst_bin_iterate_elements(GST_BIN(element));
    const GstIteratorForeachFunction rememberChild =
        [](const GValue *const item, gpointer const /*data needed by gst signature*/) {
            rememberDynamicLinks(GST_ELEMENT(g_value_get_object(item)));
        };
    const auto remember = ownsDynamicSources ? rememberPadLink : rememberChild;
    while (gst_iterator_foreach(iterator, remember, nullptr) == GST_ITERATOR_RESYNC) {
        gst_iterator_resync(iterator);
    }
}

} // namespace

GstStateChangeReturn setState(GstElement *const pipeline, const GstState state) {
    g_autoptr(GRecMutexLocker) stateLock = g_rec_mutex_locker_new(GST_STATE_GET_LOCK(pipeline));
    if (state <= GST_STATE_READY) {
        rememberDynamicLinks(pipeline);
    }
    return gst_element_set_state(pipeline, state);
}

} // namespace gst::pipeline_control
