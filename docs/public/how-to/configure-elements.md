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
permission, not the cost or success of changing a value. They do not enforce
state restrictions; OPK's property setters use `PropertyGuard` to reject writes
outside the allowed states and report a warning.

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
| `opkinfer` | `opchain-path`, `infer-id`, `audio-hop-samples`, `copy-input`, `auto-activate-model-dependencies` | NULL or READY. Returning to READY stops the OpChain and clears audio/QoS state. The next start loads the model and renegotiates media/memory. Identity must remain stable while processing. |
| `opkinfer` | `active`, `qos-enabled` | Live. Changes affect subsequent inference decisions; an execution already in progress can finish. Activation can enable upstream content providers. Disabling QoS resets skipping. |
| `opktracker` | `content-type`, `embedding-content-type`, `infer-id` | NULL or READY. Restart clears tracking history so identities from different sources cannot mix. |
| `opktracker` | All other writable properties | Live. Each frame takes a synchronized configuration snapshot. Initial Kalman covariance only affects newly created tracks; existing state is retained. See [tracker](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opktracker.md#properties). |
| `opkosd` | `enabled`, `performance-overlay-enabled`, `bg-image` | Live. Drawing uses synchronized flags and background storage. Loading a background is synchronous; a failed load leaves the current background/path unchanged and posts a warning. Empty/null clears it. |
| `opkosd` | `copy-input` | NULL or READY. Video memory/caps must be renegotiated; a pause alone does not rebuild them. |
| `opkperformance` | `enabled`, `show-all-metrics`, `update-interval` | Live. Subsequent measurement/overlay updates use the new values; no pipeline restart. |
| `opkcomm` | All writable properties | Existing live synchronous writer reopen. This can disconnect clients and discard queued records. A failed reopen posts an error and stops affected processing; the requested configuration remains available for correction. See [metadata output](https://github.com/arm/open-perception-kit/blob/main/docs/arch/elements/opkcomm.md#properties). |
| `opksink` | `qos-enabled` | Live. Updates the internal drain sink's synchronization and QoS feedback. |
| `opksink` | All other writable properties | NULL only. Servers, bindings and WebRTC sessions capture these settings at initialization; stop/start disconnects clients. |
| `opksrc` | `ws-port`, `rate` | NULL only. Signaling must restart and the receive/silence PCM output must renegotiate; existing connections are closed. |

An unsupported-state write is rejected with a bus WARNING naming the property.
Readback stays unchanged. Writes racing a state transition can also be rejected;
wait for the transition to finish before retrying. There is no pending-value
queue. In particular, setting a restricted property while
PLAYING does **not** save it for the next restart.

Serialize configuration and state requests for each element in the application's
control thread. A multi-property `g_object_set()` call is a sequence of individual
sets, not an atomic transaction. For a coordinated change, explicitly stop to the
required boundary, wait for completion with `gst_element_get_state()`, set the
properties, and restart. A PAUSED pipeline does not satisfy a READY or NULL
restriction. Using the pipeline's state API coordinates its elements; changing
an isolated element's state without coordinating its neighbours can break flow.

Monitor the bus for WARNING and ERROR messages and check state-change return
values. Setters return no success status and `notify` alone is not proof of
acceptance. GObject rejects type/range violations through its normal diagnostics;
OPK reports resource failures through the bus. A failed startup must be corrected
by the caller before retrying. OPK does not save or restore working combinations.

## Platform validation

The property API is independent of the host OS, but OPK's media, networking and
memory backends still need platform verification. Linux container results on
x86_64 do not establish native Windows or macOS support, nor Raspberry Pi/arm64
hardware behavior. WSL uses the Linux container workflow; macOS container testing
requires a separate host. See [known limitations](https://github.com/arm/open-perception-kit/blob/main/docs/arch/known-limitations.md#models-backends-and-platforms).
