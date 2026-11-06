---
title: Configure GStreamer elements
description: Discover and configure OPK element properties through the standard GStreamer API.
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Configure GStreamer elements

OPK elements expose configuration through GObject properties. Any GStreamer
application can use them; no OPK-specific application or control service is
required. This covers properties of existing elements, not changes to pipeline
topology.

## Discover the installed contract

Run these commands in the OPK development container after building:

```sh
export GST_PLUGIN_PATH="$PWD/development/build-active/meson-out${GST_PLUGIN_PATH:+:$GST_PLUGIN_PATH}"
gst-inspect-1.0 --version
gst-inspect-1.0 opkinfer
```

Use the same command for `opktracker`, `opkosd`, `opkperformance`, `opkcomm`,
`opksink`, and `opksrc`. Check the plugin **Filename** to ensure you are inspecting
the intended build. The executable is called `gst-inspect-1.0` for GStreamer 1.x,
including 1.26.

The property entries give the type, range or enum, default, read/write flags and
mutation boundary. The descriptions explain restrictions and restart effects.
Applications can obtain the same metadata with `g_object_class_list_properties()`
or Python's `element.list_properties()`. Use `GParamSpec.owner_type` to distinguish
OPK properties from inherited ones; inherited properties retain their upstream
semantics. Read-only diagnostics such as `opkinfer.qos-accepted-events-debug`
cannot be configured.

[GStreamer mutation flags](https://gstreamer.freedesktop.org/documentation/gstreamer/gstparamspec.html)
identify the highest supported state: `GST_PARAM_MUTABLE_READY` allows NULL and
READY, while `GST_PARAM_MUTABLE_PLAYING` also allows PAUSED and PLAYING. OPK
startup-only properties without a mutation flag require NULL. Flags describe
permission, not the cost or success of changing a value.

Most defaults are fixed. Read back the instance for these resolved defaults:

- Empty `infer-id` on `opkinfer` and `opktracker` resolves to the element's name.
- `opksink.static-files` resolves relative to the plugin installation.
- `opksink.webrtc-stun-server` and `webrtc-turn-server` resolve from the WebRTC
  environment settings described in [opksink](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opksink.md#properties).

Each element has a property reference with defaults, valid choices or examples,
and supported write states:
[opkinfer](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opkinfer.md#properties),
[opktracker](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opktracker.md#properties),
[opkosd](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opkosd.md#properties),
[opkperformance](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opkperformance.md#properties),
[opkcomm](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opkcomm.md#properties),
[opksink](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opksink.md#properties),
[opksrc](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opksrc.md#properties).

## Set startup values

A pipeline string uses the usual `property=value` syntax:

```sh
gst-launch-1.0 videotestsrc ! video/x-raw,format=BGRA ! \
  opkinfer opchain-path=config/models/yolo26n-320/opchain.json ! \
  opktracker iou-threshold=0.4 ! opkosd performance-overlay-enabled=false ! \
  opkcomm file-name=/tmp/results.jsonl ! fakesink
```

The model artifacts must already be downloaded. The equivalent property API in
Python is:

```python
import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst

Gst.init(None)
infer = Gst.ElementFactory.make("opkinfer", "detector")
if infer is None:
    raise RuntimeError("opkinfer plugin was not found")
infer.set_property("opchain-path", "config/models/yolo26n-320/opchain.json")
print(infer.get_property("opchain-path"))
```

C and C++ callers use `g_object_set()` and `g_object_get()` with the property type
reported by introspection. See the upstream
[element property API](https://gstreamer.freedesktop.org/documentation/application-development/basics/elements.html).
Set initial properties before starting the pipeline. Readback confirms the
configured value; resources such as models and listening sockets are validated
when the element starts. A path being readable as a property does not prove that
startup will succeed, even when inference is disabled with `active=false`.

The former `opkinfer.format` property had no effect and has been removed. Select
video formats through negotiated caps, for example
`videoconvert ! video/x-raw,format=BGRA ! opkinfer ...`. A caps filter constrains
negotiation; `videoconvert` performs conversion when needed.

## Mutation boundaries

| Element | Properties | Supported change and effects |
| --- | --- | --- |
| `opkinfer` | `opchain-path`, `infer-id`, `audio-hop-samples`, `copy-input` | NULL or READY. Returning to READY stops the OpChain and clears audio/QoS state. The next start loads the model and renegotiates media/memory. Identity must remain stable while processing. |
| `opkinfer` | `active`, `qos-enabled` | Live. Changes affect subsequent inference decisions; an execution already in progress can finish. Activation can enable upstream content providers. Disabling QoS resets skipping. |
| `opktracker` | `content-type`, `embedding-content-type`, `infer-id` | NULL or READY. Restart clears tracking history so identities from different sources cannot mix. |
| `opktracker` | All other writable properties | Live. Each frame takes a synchronized configuration snapshot. Initial Kalman covariance only affects newly created tracks; existing state is retained. See [tracker](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opktracker.md#properties). |
| `opkosd` | `enabled`, `performance-overlay-enabled`, `bg-image` | Live. Drawing uses synchronized flags and background storage. Backgrounds load asynchronously and replace the current image only after validation. Failure retains the current image and reports a failed request. Empty/null clears it. The loaded image survives stop/start; external file edits do not alter that snapshot. |
| `opkosd` | `copy-input` | NULL or READY. Video memory/caps must be renegotiated; a pause alone does not rebuild them. |
| `opkperformance` | `enabled`, `show-all-metrics`, `update-interval` | Live. Subsequent measurement/overlay updates use the new values; no pipeline restart. |
| `opkcomm` | All writable properties | Existing live synchronous writer reopen. This can disconnect clients and discard queued records. A failed reopen posts an error and stops affected processing; the requested configuration remains available for correction. See [metadata output](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opkcomm.md#properties). |
| `opksink` | `qos-enabled` | Live. Updates the internal drain sink's synchronization and QoS feedback. |
| `opksink` | All other writable properties | NULL only. Servers, bindings and WebRTC sessions capture these settings at initialization; stop/start disconnects clients. |
| `opksrc` | `ws-port`, `rate` | NULL only. Signaling must restart and the receive/silence PCM output must renegotiate; existing connections are closed. |

## Requests and outcomes

Elements exposing `property-status` accept requests through their ordinary
GObject setters and the `configure-properties` action signal. Getters return the
effective value. A restart-only request made while processing stays pending,
with its required state and reason visible beside the requested value. A normal
PAUSED/resume cycle does not apply READY- or NULL-only settings.

`configure-properties` accepts a `GstStructure` of property names and typed values
for one element and returns a request ID. Independent eligible changes apply
without waiting for restart-only changes in the same batch. A multi-property
`g_object_set()` remains a sequence of individual requests. Neither API promises
an all-or-nothing transaction.

The `property-status` structure contains the latest request for each changed
property. Each entry has `request-id`, `requested`, `effective`, `status`,
`reason`, `restart-required` and `required-state`. Statuses are `queued`,
`applying`, `restart-required`, `applied`, `failed`, `cancelled` and `superseded`.
Unknown, inherited or read-only batch fields report a failure and may omit
`effective`, `restart-required` or `required-state`; other fields can still apply.

A new request replaces an unstarted request for the same property. Setting its
effective value cancels an unstarted request. An application already in progress
finishes first, then the latest queued value is handled. Other pending properties
remain intact. Failed requests remain visible for correction; OPK keeps no
configuration history or restore operation.

The bus receives `ELEMENT` messages named `opk-property-change` with the same
fields, including outcomes for superseded requests. Property notifications follow
successful application; setter return or `notify` alone does not establish that
resources can start successfully. Type/range errors from ordinary setters retain
GObject's standard diagnostics. Fatal resource/startup errors use the usual ERROR
messages and state-change failure return.

Delivery uses GStreamer's async scheduler; setting properties does not require a
GLib main loop. The application can poll its bus or use its normal bus watch.
NULL may flush the pipeline bus, so also query `property-status` after stopping.

```python
# tracker belongs to an existing pipeline.
values = Gst.Structure.new_empty("properties")
values.set_value("iou-threshold", 0.4)
values.set_value("content-type", "replacement-content")
request_id = tracker.emit("configure-properties", values)
statuses = tracker.get_property("property-status")
print(statuses.get_value("content-type").to_string())

# Cancel an unstarted request by setting the effective value again.
tracker.set_property("content-type", tracker.get_property("content-type"))

# The caller explicitly requests any required restart.
pipeline.set_state(Gst.State.READY)
result, state, pending = pipeline.get_state(5 * Gst.SECOND)
if result == Gst.StateChangeReturn.FAILURE or state != Gst.State.READY:
    raise RuntimeError("Could not reach READY")
pipeline.set_state(Gst.State.PLAYING)
```

Keep the outer status structure alive while reading its nested entries in Python,
or copy the entry before releasing it.

To observe outcomes, attach a bus watch before submitting requests and run your
application's GLib main loop. This handler reports intermediate and final states;
match both the source element and request ID when correlating a batch.

```python
def on_element_message(bus, message):
    outcome = message.get_structure()
    if message.src == osd and outcome and outcome.get_name() == "opk-property-change":
        print(outcome.get_value("request-id"), outcome.get_value("property"),
              outcome.get_value("status"), outcome.get_value("reason"))

bus = pipeline.get_bus()
bus.add_signal_watch()
bus.connect("message::element", on_element_message)
```

```cpp
GstStructure *values = gst_structure_new("properties",
    "iou-threshold", G_TYPE_DOUBLE, 0.4,
    "content-type", G_TYPE_STRING, "replacement-content", nullptr);
guint64 requestId = 0;
g_signal_emit_by_name(tracker, "configure-properties", values, &requestId);
gst_structure_free(values);

GstStructure *statuses = nullptr;
g_object_get(tracker, "property-status", &statuses, nullptr);
if (statuses)
    gst_structure_free(statuses);
```

In an existing C++ bus callback, handle property outcomes alongside the
application's normal ERROR, EOS and state messages:

```cpp
if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ELEMENT &&
    GST_MESSAGE_SRC(message) == GST_OBJECT(osd)) {
    const GstStructure *outcome = gst_message_get_structure(message);
    if (outcome && gst_structure_has_name(outcome, "opk-property-change")) {
        guint64 outcomeRequestId = 0;
        gst_structure_get_uint64(outcome, "request-id", &outcomeRequestId);
        g_print("%" G_GUINT64_FORMAT " %s: %s (%s)\n", outcomeRequestId,
                gst_structure_get_string(outcome, "property"),
                gst_structure_get_string(outcome, "status"),
                gst_structure_get_string(outcome, "reason"));
    }
}
```

Use the pipeline state API to coordinate its elements. Changing an isolated
element's state without coordinating its neighbours can break flow. The caller
owns READY/NULL restarts; property requests never trigger them automatically.

## Platform validation

The property API is independent of the host OS, but OPK's media, networking and
memory backends still need platform verification. Linux container results on
x86_64 do not establish native Windows or macOS support, nor Raspberry Pi/arm64
hardware behavior. WSL uses the Linux container workflow; macOS container testing
requires a separate host. See [known limitations](https://github.com/arm/open-perception-kit/blob/main/docs/arch/known-limitations.md#models-backends-and-platforms).
