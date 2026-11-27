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

# opkcomm

`opkcomm` passes buffers through and publishes their FrameResults using a file,
stdout, WebSocket or TCP writer, or returns packets on demand through its
`serialize-results` action. Set `method` and its corresponding destination
properties before startup; inspect enum names and defaults with
`gst-inspect-1.0 opkcomm`. `file-name=-` selects stdout for file output.

## Properties

In the table, **Live** permits
writes in NULL, READY, PAUSED and PLAYING; **NULL/READY** and **NULL** restrict
writes to those states. Read-only entries cannot be set.

| Property | Purpose | Default | Accepted values / Example | Changeable in |
| --- | --- | --- | --- | --- |
| `method` | Choose the FrameResults writer; `none` enables action-only use. | `file` (`1`) | `none` (`0`), `file` (`1`), `websocket` (`2`), `tcp` (`3`). | Live |
| `file-name` | Output file for `method=file`. | `-` (stdout) | File path; e.g. `/tmp/results.jsonl`. `-` selects stdout. | Live |
| `ws-port` | Listening port for `method=websocket`. | `8002` | Unsigned integer, 1–65535; e.g. `9002`. | Live |
| `endpoint` | WebSocket endpoint path for `method=websocket`. | `/ws` | Path; e.g. `/metadata`. | Live |
| `tcp-host` | Bind address for `method=tcp`. | `127.0.0.1` | Address; e.g. `0.0.0.0` to listen on all IPv4 interfaces. | Live |
| `tcp-port` | Listening port for `method=tcp`. | `7001` | Unsigned integer, 1–65535; e.g. `7002`. | Live |

The existing setters also support live changes by synchronously closing and
reopening the writer. Configuration, processing and writer lifetime are
serialized. This is a disruptive operation: clients can disconnect and queued
records can be discarded. Setting several properties performs several reopen
operations; stop the pipeline first when changing a destination combination.
There is no queue redirection or record-boundary handoff yet.

If opening the writer fails, the element posts an ERROR and affected processing
returns an error instead of silently continuing without an output. The requested
properties remain readable; the caller must correct them and restart. OPK does
not restore the old writer or a previous configuration.

See [Configure GStreamer elements](../../public/how-to/configure-elements.md)
for discovery, readback, validation and the explicit restart procedure.

## Pulling a frame and its results

Place appsink after all intended metadata producers. For Cairn, the recommended
tail is `… ! opkcomm name=results method=none ! appsink name=frames`.
`method=none` opens no writer and does not serialize passing frames. The action
also works alongside the existing publishing methods and while the provider is
in NULL. It reads the supplied buffer, not the provider's most recent input.

`serialize-results` is a synchronous GObject action signal, with one `GstBuffer`
argument and a caller-owned `GstStructure` return value named
`opk-serialized-results`:

| `status` | Fields | Meaning |
| --- | --- | --- |
| `ok` | `packet`: `GBytes` | Existing FrameResults wire envelope, including valid empty results. |
| `no-metadata` | `message`: string | The buffer has no FrameResults payload. |
| `error` | `message`: string | Invalid input or serialization failure; no partial packet. |

The structure owns a reference to the immutable bytes. Native callers retain
them with `g_bytes_ref()` before freeing the structure. In Python, call `.ref()`
on the boxed value returned by `get_value()` before discarding the response.
The packet can outlive both the sample
and pipeline. Keep the sample separately while using its image memory.

Standard Python GStreamer bindings are sufficient; this function assumes the
application has already started the pipeline:

```python
def pull_frame(pipeline, timeout_ns):
    sample = pipeline.get_by_name("frames").emit("try-pull-sample", timeout_ns)
    if sample is None:
        return None  # Timeout, EOS or stopped pipeline: inspect appsink/bus as needed.
    response = pipeline.get_by_name("results").emit(
        "serialize-results", sample.get_buffer()
    )
    status = response.get_value("status")
    if status == "error":
        raise RuntimeError(response.get_value("message"))
    if status == "no-metadata":
        return sample, None
    return sample, response.get_value("packet").ref()
```

Cairn retains the returned sample and packet together for transport delivery.
The action does not map or copy pixels. Converting `GLib.Bytes` into a Python
transport's byte representation may copy metadata. The initial serialization
runs on the calling thread; later requests share the packet. The action does
not acquire writer locks or wait for a network connection.

## Packet cache and mutation

`FrameResultsMeta::readCached()` acquires a lazy `FrameResultsPacket` snapshot;
its `bytes()` method serializes on first demand. Concurrent readers share one
successful serialization. Exceptions leave no cached packet and can be retried.
After success the cache releases its source FrameResults reference, keeping only
the encoded bytes. Native consumers can use `view()` for a read-only byte span
while retaining the `FrameResultsPacket`; use `bytes()` for independent ownership.
The opkcomm worker, action, opksink metadata publisher and runtime pipeline
callbacks use this accessor. JSON/Base64 wrapping remains specific to each output.
The runtime's vector-based callback still copies bytes to preserve its API.

Cache acquisition failures are contained at the GStreamer callback boundary.
The opkcomm streaming callback reports queueing failures on the bus and returns
`GST_FLOW_ERROR`, after releasing its writer lock. The asynchronous writer logs
and drops a record when serialization or publishing throws, then continues with
the next record. It releases completed jobs before waiting for more input. The
opksink metadata path likewise logs failures without terminating its I/O thread;
runtime pipeline delivery failures go to the application error callback, whose
own exceptions are also contained.

Metadata copies share the cache, including copies made before serialization.
`FrameResultsMeta::mutate()` detaches it before invoking the mutation callback,
even for a no-op or throwing callback. Copy-on-write preserves snapshots already
retained by other readers. Old packets remain valid; the next request on changed
metadata obtains a new packet. Cache storage is released with the metadata and
its consumers, without a global history or retained image buffers.

`FrameResultsMeta::add(buffer, results)` stores an owned value; pass
`std::move(results)` to transfer it, or omit the value for an empty envelope.
Attachment does not accept mutable shared pointers. Use `has()`, `read()` and
`remove()` rather than accessing the private metadata payload/cache fields.
Modify attached results only through `mutate()` and do not retain the callback's
mutable reference. As with existing metadata access, mutation requires exclusive
access to the buffer (make it writable before branching modifications).
Concurrent serialization of an immutable snapshot is supported; concurrent
mutation/read of the same buffer is not. Direct SDK serialization bypasses this
native metadata cache. Generic metadata with caching disabled has no cache
allocation or mutex.

The native metadata layout uses `com_arm_opk_meta_FrameResultsAPI_v2` and
`com_arm_opk_meta_FrameResults_v2`; rebuild native plugins together. This does not
change the serialized envelope or generated SDK contract.
