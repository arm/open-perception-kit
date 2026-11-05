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

#include <gst/gst.h>

namespace gst {

inline GstState getPropertyMaxMutableState(const GParamSpec *const propertySpec) {
    if (propertySpec->flags & GST_PARAM_MUTABLE_PLAYING)
        return GST_STATE_PLAYING;
    if (propertySpec->flags & GST_PARAM_MUTABLE_PAUSED)
        return GST_STATE_PAUSED;
    if (propertySpec->flags & GST_PARAM_MUTABLE_READY)
        return GST_STATE_READY;
    return GST_STATE_NULL;
}

inline bool isPropertyChangeAllowedIn(const GstState state, const GParamSpec *const propertySpec) {
    // VOID_PENDING means there is no pending or next transition to restrict the change.
    return state <= getPropertyMaxMutableState(propertySpec);
}

// Holds the state-transition lock during a restricted property update. Live
// properties still need their own synchronization with processing and readback.
class PropertyGuard {
  public:
    PropertyGuard(GstElement *const propertyOwner, const GParamSpec *const propertySpec)
        : m_propertyOwner(propertyOwner) {
        if (isPropertyChangeAllowedIn(GST_STATE_PLAYING, propertySpec)) {
            m_changeAllowed = true;
            return;
        }
        // Lock state of the element, so another thread is not requesting a state change in the
        // meanwhile. Note that state transition and requesting transition can happen independently.
        m_ownsStateLock = g_rec_mutex_trylock(GST_STATE_GET_LOCK(m_propertyOwner));
        if (m_ownsStateLock) {
            // prevents writes to the shared data including the state.
            GST_OBJECT_LOCK(m_propertyOwner);
            // It is necessary to check not only the current state of the owner, but the next and
            // pending states as well, because an in progress state transition is indicated through
            // these fields.
            m_changeAllowed =
                isPropertyChangeAllowedIn(GST_STATE(m_propertyOwner), propertySpec) &&
                isPropertyChangeAllowedIn(GST_STATE_PENDING(m_propertyOwner), propertySpec) &&
                isPropertyChangeAllowedIn(GST_STATE_NEXT(m_propertyOwner), propertySpec);

            GST_OBJECT_UNLOCK(m_propertyOwner);
        }
        if (!m_changeAllowed) {
            if (m_ownsStateLock) {
                GST_STATE_UNLOCK(m_propertyOwner);
                m_ownsStateLock = false;
            }
            GST_ELEMENT_WARNING(
                m_propertyOwner,
                CORE,
                STATE_CHANGE,
                ("Property '%s' was not changed; set it in %s or a lower state "
                 "after the state transition completes",
                 propertySpec->name,
                 gst_element_state_get_name(getPropertyMaxMutableState(propertySpec))),
                ("The requested value is not queued for a later restart"));
        }
    }

    ~PropertyGuard() {
        if (m_ownsStateLock)
            GST_STATE_UNLOCK(m_propertyOwner);
    }

    PropertyGuard(const PropertyGuard &) = delete;
    PropertyGuard &operator=(const PropertyGuard &) = delete;

    bool isChangeAllowed() const {
        return m_changeAllowed;
    }

  private:
    GstElement *const m_propertyOwner;
    bool m_ownsStateLock = false;
    bool m_changeAllowed = false;
};

} // namespace gst
