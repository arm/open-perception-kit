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
#include <map>
#include <string>

namespace gst {

struct PropertyApplyResult {
    std::string reason;
    // NULL or READY defers this request to an explicit restart; VOID_PENDING is failure.
    GstState restartState = GST_STATE_VOID_PENDING;
};

// Unlisted properties succeeded. The element owns candidate validation,
// resource synchronization and effective-value storage.
using PropertyApplyResults = std::map<std::string, PropertyApplyResult>;
using ApplyProperties = PropertyApplyResults (*)(GstElement *propertyOwner,
                                                 const GstStructure *values);
using NeedsAsyncApply = bool (*)(GstElement *propertyOwner, const GParamSpec *propertySpec);

// Call last in class_init. Wraps the class's property/state callbacks and adds
// configure-properties(GstStructure) -> request ID and property-status readback.
void installPropertyChanges(GstElementClass *const elementClass,
                            const ApplyProperties applyProperties = nullptr,
                            const NeedsAsyncApply needsAsyncApply = nullptr);

} // namespace gst
