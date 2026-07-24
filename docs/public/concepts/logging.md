---
title: Logging
sidebar_position: 3
sidebar_label: Logging
description: Configure PEK log levels and targets, and understand asynchronous delivery and flushing.
---

# Logging

PEK sends normal log messages through a process-wide asynchronous logger. Calling threads format
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
| `4` | Info | All messages |

The default is `4` (`Info`).
Values out of the accepted range are silently clamped to use the default value.

The C++ API can read or replace the process setting at runtime:

```cpp
pek::setLogLevel(2);
const int currentLevel = pek::getLogLevel();
```

`setLogLevel()` clamps values to the supported `0` to `4` range.

## Log targets

`OPK_LOG_TARGETS` is a comma-separated list of targets to enable initially. Target names are
case-sensitive.

| Value | Initial targets |
| --- | --- |
| `stdout` | Standard output |
| `stderr` | Standard error |
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
OPK_LOG_TARGETS=stdout ./tools/pek-menu

# Write every enabled severity to stderr.
OPK_LOG_TARGETS=stderr ./tools/pek-menu

# Duplicate every enabled severity to both streams.
OPK_LOG_TARGETS=stdout,stderr ./tools/pek-menu

# Suppress asynchronous output.
OPK_LOG_TARGETS=none ./tools/pek-menu
```

The built-in stdout and stderr targets always exist. The C++ API changes whether an available
target is enabled; it does not create or remove targets:

```cpp
pek::setLogTargetState(pek::LogTargetType::Stdout, false);
pek::setLogTargetState(pek::LogTargetType::Stderr, true);

const auto enabledTargets = pek::getEnabledLogTargets();
```

`setLogTargetState()` returns `false` when the requested target is unavailable. A buffered record
uses the target states that are active when the worker dispatches it.

## Environment initialization

Environment settings are read once, when process logging is first initialized. Later API calls
change the in-process settings and do not modify the environment.

If `OPK_LOG_LEVEL` or `OPK_LOG_TARGETS` is unset, PEK reports the chosen default synchronously on
stderr. When both are unset, the level notice is written first:

```text
OPK_LOG_LEVEL is not set; defaulting to 4 (Info).
OPK_LOG_TARGETS is not set; defaulting to stdout.
```

These notices are unconditional and do not pass through level filtering or asynchronous target
selection. Malformed values fall back silently.

## Message formatting and destinations

The severity functions are:

| Function | Level | Output decoration |
| --- | --- | --- |
| `pek::log()` | Info | Message unchanged |
| `pek::logn()` | Notice | ANSI inverted text |
| `pek::logw()` | Warn | `W: ` prefix |
| `pek::loge()` | Error | `E: ` prefix |

The corresponding `logRuntime()`, `logwRuntime()`, and `logeRuntime()` functions accept a runtime
format string. Prefer the compile-time-checked functions when the format string is known at build
time.

`forceLog()` writes synchronously and unconditionally to stdout. `forceLoge()` does the same for
stderr. They bypass the log level, asynchronous buffer, and target states.

## Buffering and overload

The asynchronous logger has a fixed circular buffer for 1024 records. It never waits for buffer
space and does not create an unbounded backlog.

When the buffer is full, a newly accepted record replaces the oldest record still waiting in the
buffer. A record already removed by the worker is in flight and is not overwritten. The remaining
records preserve their order. There is currently no public dropped-record counter.

Keep hot-path messages concise and avoid high-volume logging. Drop-oldest behavior keeps the most
recent context available during overload, but overwritten messages cannot be recovered.

## Flushing and shutdown

`pek::logFlush()` waits until every record accepted before the call has either been dispatched to
the enabled targets or overwritten by the drop-oldest policy. It then flushes every enabled
target. It does not delete pending records, and it cannot restore overwritten records.

Records accepted concurrently after the flush boundary do not extend the wait indefinitely. They
may still be dispatched before the target flush occurs.

During normal process shutdown, the logger drains the records that remain in its buffer, flushes
the enabled targets, and joins its worker thread. Records overwritten before shutdown remain lost.

## Adding a built-in target

Log targets are a private, build-time extension surface. Runtime registration, removal, and
ownership transfer are not supported.

```text
PEK and GStreamer components
            |
            v
      public Log.h API
            |
            v
 private queue and worker -----> built-in stdout target
            |                  -> built-in stderr target
            |                  -> future built-in file or adapter target
            |
            +---- no dependency on GStreamer or GLib
```

A new built-in target adds a `LogTargetType`, its environment name, and an implementation created
by the built-in target factory. The logger owns targets through `std::unique_ptr`. It invokes
`write()` on the worker thread. `flush()` can run on the worker during shutdown or on the thread
that calls `logFlush()` after its buffer barrier completes. Target writes, flushes, and state
changes are serialized. Target implementations must report failures by throwing; the logger
catches the failure and disables only that target.

Targets must not call the asynchronous logging API, `logFlush()`, or target-state functions,
because doing so can recurse into the logger or deadlock. They may use `forceLog()` or
`forceLoge()` for exceptional internal diagnostics, but not for ordinary record delivery.

A future file target can use the built-in target factory directly. A future webpage/`peksink` or
GStreamer-facing integration will need an adapter boundary owned by the module that uses that
technology. The logging core must not depend on those adapters, GStreamer, or GLib. Do not add an
unbounded queue behind a target or change the logger's drop-oldest policy.

[Back to Concepts](/concepts)
