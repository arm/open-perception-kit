---
title: Logging
sidebar_position: 3
sidebar_label: Logging
description: Configure OPK log levels and targets, and understand asynchronous delivery and flushing.
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


# Logging

OPK sends normal log messages through a process-wide asynchronous logger. Calling threads format
and filter a message, place it in a fixed-capacity buffer, and continue. After one-time environment
initialization, a separate worker writes the message to every enabled log target.

The logger is implemented with the C++ standard library and `fmt`. It does not depend on
GStreamer or GLib, so using it does not change the synchronous execution model of the media and
inference pipeline.

## Log levels

`OPK_LOG_LEVEL` sets the initial verbosity:

| Value | Level | Messages included |
| --- | --- | --- |
| `0` | Off | None |
| `1` | Error | Errors |
| `2` | Warn | Warnings and errors |
| `3` | Notice | Notices, warnings, and errors |
| `4` | Info | Informational messages and less verbose levels |
| `5` | Debug | All messages |

The default is `4` (`Info`).
Single digits above `5` are silently clamped to `5`. Malformed and multi-digit values silently use
the default.

The C++ API can read or replace the process setting at runtime:

```cpp
opk::log::setLogLevel(2);
const int currentLevel = opk::log::getLogLevel();
```

`opk::log::setLogLevel()` clamps values to the supported `0` to `5` range.

`opk::runtime::Pipeline::start()` applies the level in its `StartOptions` before
playback begins. The default is `LogLevel::Error`. Applications can select a
different level for a run without separate logger setup:

```cpp
opk::runtime::Pipeline::StartOptions options;
options.logLevel = opk::runtime::LogLevel::Debug;
pipeline.start(options);
```

## Log targets

`OPK_LOG_TARGETS` is a comma-separated list of targets to enable initially. Target names are
case-sensitive.

| Value | Initial targets |
| --- | --- |
| `stdout` | Standard output |
| `stderr` | Standard error |
| `file` | Raw messages appended to `OPK_LOG_FILE` |
| `stdout,stderr` | Standard output and standard error |
| `none` | No targets |

Each accepted message goes to every enabled target, regardless of severity. A warning or error is
not routed to stderr automatically. For example, with `OPK_LOG_TARGETS=stdout`, errors are written
only to stdout; with `OPK_LOG_TARGETS=stdout,stderr`, the same error is written to both streams.

Duplicate and unknown list entries are ignored while the rest of the list is processed. If no
recognized target remains, stdout is used. Only the exact value `none` disables every target.

Examples:

```bash
# Write every enabled severity to stdout.
./tools/opk-menu --log-targets stdout

# Write every enabled severity to stderr.
./tools/opk-menu --log-targets stderr

# Duplicate every enabled severity to both streams.
./tools/opk-menu --log-targets stdout,stderr

# Append raw messages to opk.log.
./tools/opk-menu --log-targets file

# Append raw messages to a selected file and keep console output.
OPK_LOG_FILE=/tmp/opk.log ./tools/opk-menu --log-targets stdout,file

# Suppress asynchronous output.
./tools/opk-menu --log-targets none

# Enable every severity and write it to stderr.
./tools/opk-menu --log-level debug --log-targets stderr
```

The built-in stdout, stderr, and file targets always exist. The C++ API changes whether an available
target is enabled; it does not create or remove targets:

```cpp
opk::log::setLogTargetState(opk::log::TargetType::Stdout, false);
opk::log::setLogTargetState(opk::log::TargetType::Stderr, true);
opk::log::setLogTargetState(opk::log::TargetType::File, true);

const auto enabledTargets = opk::log::getEnabledLogTargets();
```

`opk::log::setLogTargetState()` returns `false` when the requested target is unavailable. A buffered record
uses the target states that are active when the worker dispatches it.

`Pipeline::StartOptions` exposes the same target states as
`logToStdout`, `logToStderr`, and `logToFile`. Its defaults disable stdout and
file output and enable stderr. As with the direct logging API, these settings
are process-wide rather than private to one pipeline. Starting another pipeline
with different options replaces the active target states.

The file target is disabled unless the exact `file` token is selected or the C++ API enables it.
`OPK_LOG_FILE` only selects its path; it does not enable the target. An unset or empty
`OPK_LOG_FILE` uses `opk.log` in the process working directory.

The file is opened only when the first enabled message is written. It is opened in binary append
mode, so existing content is preserved and messages contain exactly the formatted log record:
no target-added severity prefix, ANSI styling, timestamp, or line ending. Debug records already
contain their cyan/reset ANSI bytes, source prefix, and line ending; those bytes are preserved in
the raw file. Disabling the target closes the file immediately. Re-enabling opens it lazily on the
next message. Parent directories are not created automatically.

## Environment initialization

Environment settings are read once, when process logging is first initialized. Later API calls
change the in-process settings and do not modify the environment.

If `OPK_LOG_LEVEL` or `OPK_LOG_TARGETS` is unset, OPK silently uses the defaults: `Info` level and
the `stdout` target. Malformed values also fall back silently.

These environment values initialize the process logger. Starting a Runtime
`Pipeline` subsequently replaces the active level and targets with that call's
`StartOptions`. `opk-menu` exposes those values through `--log-level` and
`--log-targets`; its playback defaults are `error` and `stderr`.

## Message formatting and destinations

The severity functions are:

| Function | Level | Output decoration |
| --- | --- | --- |
| `opk::log::debug()` | Debug | Cyan `[source-file:line] ` prefix, reset, and one trailing newline |
| `opk::log::info()` | Info | Message unchanged |
| `opk::log::notice()` | Notice | ANSI inverted text |
| `opk::log::warning()` | Warn | `W: ` prefix |
| `opk::log::error()` | Error | `E: ` prefix |

All severity functions require compile-time-checked format strings. For example:

```cpp
opk::log::debug("WebSocket server listening on port {}", port);
```

These functions escape control characters, quotes and backslashes in string arguments.
Pass original strings and put intended line breaks in the format string.
For other logging APIs, such as GStreamer's, use `opk::log::escape(text)`.

Debug calls are present in both debug and release builds. Arguments are evaluated and the message
is formatted before level filtering, so avoid expensive expressions in frequently reached Debug
calls. One Debug call enqueues one complete record, preventing its prefix, payload, and newline
from interleaving with other records.

`opk::log::instantInfo()` and `opk::log::instantError()` write synchronously to stdout and stderr,
even before logger initialization. They bypass log levels, buffering and configured targets.
`opk-menu` uses them for UI output and early diagnostics. They preserve raw text, including ANSI
styling and line breaks; use `opk::log::escape()` when inserting external text.

## Buffering and overload

The asynchronous logger has a fixed circular buffer for 1024 records. It never waits for buffer
space and does not create an unbounded backlog.

When the buffer is full, a newly accepted record replaces the oldest record still waiting in the
buffer. A record already removed by the worker is in flight and is not overwritten. The remaining
records preserve their order. There is currently no public dropped-record counter.

Keep hot-path messages concise and avoid high-volume logging. Drop-oldest behavior keeps the most
recent context available during overload, but overwritten messages cannot be recovered.

## Flushing and shutdown

`opk::log::flush()` waits until every record accepted before the call has either been dispatched to
the enabled targets or overwritten by the drop-oldest policy. It then flushes every enabled
target. It does not delete pending records, and it cannot restore overwritten records.
If the logger has not been initialized, `flush()` is a no-op: it does not start
the worker or capture environment settings. Before launching a pipeline,
`opk-menu` flushes queued diagnostics in the parent process, before `exec` or
`fork`, because successful `exec` does not run normal logger shutdown.

Records accepted concurrently after the flush boundary do not extend the wait indefinitely. They
may still be dispatched before the target flush occurs.

During normal process shutdown, the logger drains the records that remain in its buffer, flushes
the enabled targets, and joins its worker thread. Records overwritten before shutdown remain lost.

## Adding a built-in target

Log targets are a private, build-time extension surface. Runtime registration, removal, and
ownership transfer are not supported.

```text
OPK and GStreamer components
            |
            v
      public Log.h API
            |
            v
 private queue and worker -----> built-in stdout target
            |                  -> built-in stderr target
            |                  -> built-in raw file target
            |
            +---- no dependency on GStreamer or GLib
```

A new built-in target adds a `opk::log::TargetType`, its environment name, and an
implementation created by the built-in target factory. The logger owns targets through
`std::unique_ptr`. It invokes `write()` on the worker thread. `flush()` can run on the worker during
shutdown or on the thread that calls `opk::log::flush()` after its buffer barrier completes.
Target writes, flushes, and state changes are serialized. Target implementations must report
failures by throwing; the logger catches the failure and disables only that target.

For the file target, open, write, or flush failure closes and disables only the file destination.
Other enabled targets continue receiving messages.

Targets must not call the asynchronous logging API, `opk::log::flush()`, or target-state
functions, because doing so can recurse into the logger or deadlock. They may use
`opk::log::instantInfo()` or `opk::log::instantError()` for exceptional internal diagnostics,
but not for ordinary record delivery.

A future webpage/`opksink` or GStreamer-facing integration will need an adapter boundary owned by
the module that uses that technology. The logging core must not depend on those adapters,
GStreamer, or GLib. Do not add an unbounded queue behind a target or change the logger's
drop-oldest policy.

[Back to Concepts](/concepts)
