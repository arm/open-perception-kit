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

#include "PropertyChanges.h"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace gst {
namespace {

GstState getPropertyMaxMutableState(const GParamSpec *const propertySpec) {
    if (propertySpec->flags & GST_PARAM_MUTABLE_PLAYING)
        return GST_STATE_PLAYING;
    if (propertySpec->flags & GST_PARAM_MUTABLE_PAUSED)
        return GST_STATE_PAUSED;
    if (propertySpec->flags & GST_PARAM_MUTABLE_READY)
        return GST_STATE_READY;
    return GST_STATE_NULL;
}

using Structure = std::shared_ptr<GstStructure>;

Structure ownStructure(GstStructure *const structure) {
    return {structure, gst_structure_free};
}

GQuark classKey() {
    return g_quark_from_static_string("opk-property-change-handlers");
}

GQuark instanceKey() {
    return g_quark_from_static_string("opk-property-changes");
}

struct PropertyHandlers {
    GObjectSetPropertyFunc setProperty;
    GObjectGetPropertyFunc getProperty;
    GstStateChangeReturn (*changeState)(GstElement *, GstStateChange);
    ApplyProperties applyProperties;
    NeedsAsyncApply needsAsyncApply;
    GParamSpec *statusProperty;
};

const PropertyHandlers &propertyHandlers(GstElement *const propertyOwner) {
    for (auto ownerType = G_OBJECT_TYPE(propertyOwner);; ownerType = g_type_parent(ownerType)) {
        if (auto *const callbacks =
                static_cast<PropertyHandlers *>(g_type_get_qdata(ownerType, classKey())))
            return *callbacks;
    }
}

struct PropertyRequest {
    GParamSpec *propertySpec;
    guint64 requestId;
    Structure value;
    GstState maxMutableState;
    std::string restartReason;

    const GValue *requestedValue() const {
        return gst_structure_get_value(value.get(), "requested");
    }
};

class PropertyChanges {
  public:
    explicit PropertyChanges(GstElement *const propertyOwner) : m_propertyOwner(propertyOwner) {}

    guint64 request(const GstStructure *const values);
    GstStructure *status();
    void applyPending(const GstState boundary = GST_STATE_VOID_PENDING,
                      const bool applyAsync = false);

  private:
    Structure readEffectiveValue(GParamSpec *const propertySpec) const;
    void recordStatus(const PropertyRequest &change,
                      const char *const status,
                      const char *const reason = "");
    void publishStatusMessages();
    void apply(std::vector<PropertyRequest> changes);
    bool canApply(const PropertyRequest &change, const GstState boundary) const;

    GstElement *const m_propertyOwner;
    std::mutex m_mutex;
    guint64 m_nextRequestId = 0;
    std::map<std::string, PropertyRequest> m_pending;
    std::map<std::string, guint64> m_applying;
    std::map<std::string, Structure> m_status;
    std::deque<Structure> m_messages;
    bool m_publisherScheduled = false;
    bool m_workerScheduled = false;
    bool m_stateRetryScheduled = false;
};

PropertyChanges &changesFor(GstElement *const propertyOwner) {
    const auto key = instanceKey();
    auto *changes =
        static_cast<PropertyChanges *>(g_object_get_qdata(G_OBJECT(propertyOwner), key));
    if (changes)
        return *changes;
    auto candidate = std::make_unique<PropertyChanges>(propertyOwner);
    if (g_object_replace_qdata(
            G_OBJECT(propertyOwner),
            key,
            nullptr,
            candidate.get(),
            [](gpointer data) { delete static_cast<PropertyChanges *>(data); },
            nullptr)) {
        return *candidate.release();
    }
    return *static_cast<PropertyChanges *>(g_object_get_qdata(G_OBJECT(propertyOwner), key));
}

Structure PropertyChanges::readEffectiveValue(GParamSpec *const propertySpec) const {
    GValue value = G_VALUE_INIT;
    g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE(propertySpec));
    propertyHandlers(m_propertyOwner)
        .getProperty(G_OBJECT(m_propertyOwner), propertySpec->param_id, &value, propertySpec);
    auto result = ownStructure(gst_structure_new_empty("effective-value"));
    gst_structure_set_value(result.get(), "effective", &value);
    g_value_unset(&value);
    return result;
}

// Called with m_mutex held. Delivery runs separately, with no element or request
// locks, because synchronous bus/notify callbacks may stop the whole pipeline.
void PropertyChanges::recordStatus(const PropertyRequest &change,
                                   const char *const state,
                                   const char *const reason) {
    auto message = ownStructure(gst_structure_copy(change.value.get()));
    gst_structure_set_name(message.get(), "opk-property-change");
    gst_structure_set(message.get(),
                      "property",
                      G_TYPE_STRING,
                      change.propertySpec->name,
                      "request-id",
                      G_TYPE_UINT64,
                      change.requestId,
                      "status",
                      G_TYPE_STRING,
                      state,
                      "reason",
                      G_TYPE_STRING,
                      reason,
                      "restart-required",
                      G_TYPE_BOOLEAN,
                      g_str_equal(state, "restart-required"),
                      "required-state",
                      GST_TYPE_STATE,
                      change.maxMutableState,
                      nullptr);
    const auto effective = readEffectiveValue(change.propertySpec);
    gst_structure_set_value(
        message.get(), "effective", gst_structure_get_value(effective.get(), "effective"));
    const auto previous = m_status.find(change.propertySpec->name);
    guint64 previousId = 0;
    if (previous != m_status.end())
        gst_structure_get_uint64(previous->second.get(), "request-id", &previousId);
    if (previousId <= change.requestId)
        m_status.insert_or_assign(change.propertySpec->name, message);
    m_messages.push_back(std::move(message));
    if (!std::exchange(m_publisherScheduled, true)) {
        gst_element_call_async(
            m_propertyOwner,
            [](GstElement *owner, gpointer) { changesFor(owner).publishStatusMessages(); },
            nullptr,
            nullptr);
    }
}

void PropertyChanges::publishStatusMessages() {
    for (;;) {
        Structure message;
        {
            const std::scoped_lock lock(m_mutex);
            if (m_messages.empty()) {
                m_publisherScheduled = false;
                return;
            }
            message = std::move(m_messages.front());
            m_messages.pop_front();
        }
        if (g_str_equal(gst_structure_get_string(message.get(), "status"), "applied"))
            g_object_notify(G_OBJECT(m_propertyOwner),
                            gst_structure_get_string(message.get(), "property"));
        g_object_notify(G_OBJECT(m_propertyOwner), "property-status");
        gst_element_post_message(m_propertyOwner,
                                 gst_message_new_element(GST_OBJECT(m_propertyOwner),
                                                         gst_structure_copy(message.get())));
    }
}

guint64 PropertyChanges::request(const GstStructure *const values) {
    if (!values)
        return 0;
    guint64 requestId;
    {
        const std::scoped_lock lock(m_mutex);
        requestId = ++m_nextRequestId;
        for (int index = 0; index < gst_structure_n_fields(values); ++index) {
            const char *const fieldName = gst_structure_nth_field_name(values, index);
            auto *const propertySpec =
                g_object_class_find_property(G_OBJECT_GET_CLASS(m_propertyOwner), fieldName);
            if (!propertySpec ||
                propertySpec->owner_type !=
                    propertyHandlers(m_propertyOwner).statusProperty->owner_type ||
                !(propertySpec->flags & G_PARAM_WRITABLE)) {
                // Invalid batch fields cannot be represented as element properties.
                auto failure = ownStructure(
                    gst_structure_new("opk-property-change",
                                      "property",
                                      G_TYPE_STRING,
                                      fieldName,
                                      "request-id",
                                      G_TYPE_UINT64,
                                      requestId,
                                      "status",
                                      G_TYPE_STRING,
                                      "failed",
                                      "reason",
                                      G_TYPE_STRING,
                                      "Expected a writable property defined by this element",
                                      nullptr));
                gst_structure_set_value(
                    failure.get(), "requested", gst_structure_get_value(values, fieldName));
                m_status.insert_or_assign(fieldName, failure);
                m_messages.push_back(std::move(failure));
                continue;
            }
            const char *const name = propertySpec->name;
            GValue requested = G_VALUE_INIT;
            g_value_init(&requested, G_PARAM_SPEC_VALUE_TYPE(propertySpec));
            const bool validType =
                g_value_transform(gst_structure_get_value(values, fieldName), &requested);
            const bool validValue = validType && !g_param_value_validate(propertySpec, &requested);
            auto value = ownStructure(gst_structure_new_empty("property-value"));
            gst_structure_set_value(value.get(),
                                    "requested",
                                    validValue ? &requested
                                               : gst_structure_get_value(values, fieldName));
            g_value_unset(&requested);
            const PropertyRequest change{propertySpec,
                                         requestId,
                                         std::move(value),
                                         getPropertyMaxMutableState(propertySpec),
                                         g_param_spec_get_blurb(propertySpec)};
            if (!validValue) {
                if (const auto previous = m_pending.find(name); previous != m_pending.end()) {
                    recordStatus(previous->second, "superseded");
                    m_pending.erase(previous);
                }
                recordStatus(
                    change, "failed", "Value does not match the property's type or accepted range");
                continue;
            }
            const auto effective = readEffectiveValue(propertySpec);
            const bool alreadyEffective =
                !m_applying.contains(name) &&
                g_param_values_cmp(propertySpec,
                                   change.requestedValue(),
                                   gst_structure_get_value(effective.get(), "effective")) == 0;
            if (const auto previous = m_pending.find(name); previous != m_pending.end()) {
                recordStatus(previous->second, alreadyEffective ? "cancelled" : "superseded");
                m_pending.erase(previous);
            }
            if (alreadyEffective) {
                recordStatus(change, "applied", "Value is already effective");
            } else {
                m_pending.insert_or_assign(name, change);
                recordStatus(change, "queued");
            }
        }
        if (!m_messages.empty() && !std::exchange(m_publisherScheduled, true))
            gst_element_call_async(
                m_propertyOwner,
                [](GstElement *owner, gpointer) { changesFor(owner).publishStatusMessages(); },
                nullptr,
                nullptr);
    }
    applyPending();
    return requestId;
}

GstStructure *PropertyChanges::status() {
    const std::scoped_lock lock(m_mutex);
    if (m_status.empty())
        return nullptr;
    auto *const result = gst_structure_new_empty("opk-property-status");
    for (const auto &[name, value] : m_status) {
        g_autoptr(GstStructure) entry = gst_structure_copy(value.get());
        auto *const propertySpec =
            g_object_class_find_property(G_OBJECT_GET_CLASS(m_propertyOwner), name.c_str());
        if (propertySpec &&
            propertySpec->owner_type ==
                propertyHandlers(m_propertyOwner).statusProperty->owner_type &&
            propertySpec != propertyHandlers(m_propertyOwner).statusProperty) {
            const auto effective = readEffectiveValue(propertySpec);
            gst_structure_set_value(
                entry, "effective", gst_structure_get_value(effective.get(), "effective"));
        }
        gst_structure_set(result, name.c_str(), GST_TYPE_STRUCTURE, entry, nullptr);
    }
    return result;
}

bool PropertyChanges::canApply(const PropertyRequest &change, const GstState boundary) const {
    if (boundary != GST_STATE_VOID_PENDING)
        return boundary <= change.maxMutableState;
    if (change.maxMutableState == GST_STATE_PLAYING)
        return true;
    g_autoptr(GMutexLocker) objectLock = g_mutex_locker_new(GST_OBJECT_GET_LOCK(m_propertyOwner));
    return GST_STATE(m_propertyOwner) <= change.maxMutableState &&
           GST_STATE_PENDING(m_propertyOwner) <= change.maxMutableState &&
           GST_STATE_NEXT(m_propertyOwner) <= change.maxMutableState;
}

void PropertyChanges::apply(std::vector<PropertyRequest> changes) {
    auto values = ownStructure(gst_structure_new_empty("property-values"));
    for (const auto &change : changes)
        gst_structure_set_value(values.get(), change.propertySpec->name, change.requestedValue());
    const auto &callbacks = propertyHandlers(m_propertyOwner);
    PropertyApplyResults failures;
    try {
        if (callbacks.applyProperties) {
            failures = callbacks.applyProperties(m_propertyOwner, values.get());
        } else {
            for (const auto &change : changes)
                callbacks.setProperty(G_OBJECT(m_propertyOwner),
                                      change.propertySpec->param_id,
                                      change.requestedValue(),
                                      change.propertySpec);
        }
    } catch (const std::exception &exception) {
        for (const auto &change : changes)
            failures.emplace(change.propertySpec->name, PropertyApplyResult{exception.what()});
    }
    const std::scoped_lock lock(m_mutex);
    for (auto &change : changes) {
        const auto failure = failures.find(change.propertySpec->name);
        if (failure == failures.end()) {
            recordStatus(change, "applied");
        } else if (failure->second.restartState == GST_STATE_VOID_PENDING) {
            recordStatus(change, "failed", failure->second.reason.c_str());
        } else {
            change.maxMutableState = failure->second.restartState;
            change.restartReason = failure->second.reason;
            if (m_pending.contains(change.propertySpec->name)) {
                recordStatus(change, "superseded", "A newer request is queued");
            } else {
                m_pending.emplace(change.propertySpec->name, change);
                recordStatus(change, "restart-required", change.restartReason.c_str());
            }
        }
        m_applying.erase(change.propertySpec->name);
    }
}

void PropertyChanges::applyPending(const GstState boundary, const bool applyAsync) {
    {
        const std::scoped_lock lock(m_mutex);
        if (m_pending.empty()) {
            if (applyAsync)
                m_workerScheduled = false;
            return;
        }
    }
    const bool ownsStateLock = g_rec_mutex_trylock(GST_STATE_GET_LOCK(m_propertyOwner));
    std::vector<PropertyRequest> immediate;
    std::vector<PropertyRequest> asynchronous;
    bool retryAfterTransition = false;
    bool scheduleWorker = false;
    const auto &callbacks = propertyHandlers(m_propertyOwner);
    {
        const std::scoped_lock lock(m_mutex);
        for (auto pending = m_pending.begin(); pending != m_pending.end();) {
            const auto &change = pending->second;
            const bool live = change.maxMutableState == GST_STATE_PLAYING;
            const bool needsAsync = callbacks.needsAsyncApply &&
                                    callbacks.needsAsyncApply(m_propertyOwner, change.propertySpec);
            if (m_applying.contains(pending->first)) {
                ++pending;
            } else if (!canApply(change, boundary)) {
                recordStatus(change, "restart-required", change.restartReason.c_str());
                ++pending;
            } else if (!live && !ownsStateLock) {
                retryAfterTransition |= !std::exchange(m_stateRetryScheduled, true);
                ++pending;
            } else if (live && needsAsync && !applyAsync) {
                scheduleWorker |= !std::exchange(m_workerScheduled, true);
                ++pending;
            } else {
                m_applying.emplace(pending->first, change.requestId);
                recordStatus(change, "applying");
                (live && needsAsync ? asynchronous : immediate).push_back(change);
                pending = m_pending.erase(pending);
            }
        }
    }
    if (!immediate.empty())
        apply(std::move(immediate));
    if (ownsStateLock)
        GST_STATE_UNLOCK(m_propertyOwner);
    if (retryAfterTransition) {
        gst_element_call_async(
            m_propertyOwner,
            [](GstElement *owner, gpointer) {
                auto &changes = changesFor(owner);
                GST_STATE_LOCK(owner);
                {
                    const std::scoped_lock lock(changes.m_mutex);
                    changes.m_stateRetryScheduled = false;
                }
                changes.applyPending();
                GST_STATE_UNLOCK(owner);
            },
            nullptr,
            nullptr);
    }
    if (scheduleWorker) {
        gst_element_call_async(
            m_propertyOwner,
            [](GstElement *owner, gpointer) {
                changesFor(owner).applyPending(GST_STATE_VOID_PENDING, true);
            },
            nullptr,
            nullptr);
    }
    if (applyAsync) {
        if (!asynchronous.empty())
            apply(std::move(asynchronous));
        {
            const std::scoped_lock lock(m_mutex);
            m_workerScheduled = false;
        }
        applyPending();
    } else {
        const std::scoped_lock lock(m_mutex);
        const bool hasQueuedImmediateRequest =
            std::any_of(m_pending.begin(), m_pending.end(), [this, &callbacks](const auto &entry) {
                return !m_applying.contains(entry.first) &&
                       (!callbacks.needsAsyncApply ||
                        !callbacks.needsAsyncApply(m_propertyOwner, entry.second.propertySpec)) &&
                       g_str_equal(
                           gst_structure_get_string(m_status.at(entry.first).get(), "status"),
                           "queued");
            });
        if (hasQueuedImmediateRequest && !m_stateRetryScheduled) {
            // A callback during application can enqueue another value for the same property.
            gst_element_call_async(
                m_propertyOwner,
                [](GstElement *owner, gpointer) { changesFor(owner).applyPending(); },
                nullptr,
                nullptr);
        }
    }
}

void setProperty(GObject *const object,
                 const guint propertyId,
                 const GValue *const value,
                 GParamSpec *const propertySpec) {
    auto *const owner = GST_ELEMENT(object);
    if (propertySpec->owner_type != propertyHandlers(owner).statusProperty->owner_type) {
        propertyHandlers(owner).setProperty(object, propertyId, value, propertySpec);
        return;
    }
    g_autoptr(GstStructure) values = gst_structure_new_empty("property-values");
    gst_structure_set_value(values, propertySpec->name, value);
    changesFor(owner).request(values);
}

void getProperty(GObject *const object,
                 const guint propertyId,
                 GValue *const value,
                 GParamSpec *const propertySpec) {
    auto *const owner = GST_ELEMENT(object);
    if (propertySpec == propertyHandlers(owner).statusProperty)
        g_value_take_boxed(value, changesFor(owner).status());
    else
        propertyHandlers(owner).getProperty(object, propertyId, value, propertySpec);
}

GstStateChangeReturn changeState(GstElement *const owner, const GstStateChange transition) {
    const auto result = propertyHandlers(owner).changeState(owner, transition);
    if (result != GST_STATE_CHANGE_FAILURE &&
        GST_STATE_TRANSITION_NEXT(transition) < GST_STATE_TRANSITION_CURRENT(transition)) {
        changesFor(owner).applyPending(GST_STATE_TRANSITION_NEXT(transition));
    }
    return result;
}

} // namespace

void installPropertyChanges(GstElementClass *const elementClass,
                            const ApplyProperties applyProperties,
                            const NeedsAsyncApply needsAsyncApply) {
    auto *const objectClass = G_OBJECT_CLASS(elementClass);
    guint propertyCount = 0;
    g_autofree GParamSpec **properties =
        g_object_class_list_properties(objectClass, &propertyCount);
    guint statusPropertyId = 1;
    for (guint index = 0; index < propertyCount; ++index) {
        auto *const propertySpec = properties[index];
        if (propertySpec->owner_type != G_TYPE_FROM_CLASS(objectClass))
            continue;
        statusPropertyId = std::max(statusPropertyId, propertySpec->param_id + 1);
        if (propertySpec->flags & G_PARAM_WRITABLE)
            propertySpec->flags =
                static_cast<GParamFlags>(propertySpec->flags | G_PARAM_EXPLICIT_NOTIFY);
    }
    auto *const statusProperty =
        g_param_spec_boxed("property-status",
                           "Property status",
                           "Latest requested/effective values, outcome and restart requirement for "
                           "each changed property",
                           GST_TYPE_STRUCTURE,
                           static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
    auto *const callbacks = new PropertyHandlers{objectClass->set_property,
                                                 objectClass->get_property,
                                                 elementClass->change_state,
                                                 applyProperties,
                                                 needsAsyncApply,
                                                 statusProperty};
    g_type_set_qdata(G_TYPE_FROM_CLASS(objectClass), classKey(), callbacks);
    objectClass->set_property = setProperty;
    objectClass->get_property = getProperty;
    elementClass->change_state = changeState;
    g_object_class_install_property(objectClass, statusPropertyId, statusProperty);
    g_signal_new_class_handler(
        "configure-properties",
        G_TYPE_FROM_CLASS(objectClass),
        static_cast<GSignalFlags>(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        G_CALLBACK(+[](GstElement *owner, const GstStructure *values) -> guint64 {
            return changesFor(owner).request(values);
        }),
        nullptr,
        nullptr,
        nullptr,
        G_TYPE_UINT64,
        1,
        GST_TYPE_STRUCTURE);
}

} // namespace gst
