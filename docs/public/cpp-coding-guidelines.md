---
sidebar_position: 12
sidebar_label: C++ Coding Guidelines
---
<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->


# C++ Coding Guidelines

This page defines the default C++ coding rules for OPK runtime code.

The goal is not to force a large style rewrite. The goal is to make new code
safer, easier to review, and harder to break with small mistakes.

## What will you learn from this documentation?

If you follow this page successfully, you will know which C++ defaults OPK
expects for security, readability, and routine correctness.

At the end of this page, you should be able to judge whether a change fits the
existing OPK code style, whether it handles failure paths clearly, and whether
it is suitable for automatic checking with `clang-tidy`.

## Scope

These rules apply first to code under `development/`.

Use them most strictly for:

- runtime code
- parsing and serialization code
- file, network, and process boundaries
- postprocessing and tensor decoding code

When working inside third-party code under `development/subprojects/`, preserve
the upstream style instead of rewriting it to match OPK.

## The priorities

When there is a tradeoff, prefer this order:

1. correctness and security
2. human readability
3. performance based on measured need
4. cleverness

If an optimization makes the code harder to reason about, it needs a clear
measured reason.

## Prefer the intended OPK extension surfaces

Before changing core runtime code, check whether the task belongs in:

- `config/models/`
- `config/opchains/`
- `config/pipelines/`
- `development/ops-std/postproc/`

Do not introduce new runtime abstractions when an existing parser, model
descriptor, opchain, or pipeline surface already fits the job.

## Security and boundary handling

Treat all external input as untrusted until it is validated.

This includes:

- JSON and HTTP input
- file paths and file contents
- environment variables
- model metadata
- tensor shapes, tensor counts, and tensor element types

Required rules:

- Validate sizes, ranges, enum-like strings, and required fields before use.
- Validate tensor rank and element count before indexing or decoding.
- Reject invalid UTF-8 or invalid schema assumptions explicitly.
- Do not assume paths are safe just because they came from config or a request.
- Do not log secrets, credentials, tokens, or large untrusted payloads.
- Keep failure behavior explicit. Invalid input must not silently become a
  partially valid runtime state.

## Readability rules

Write code so the next reviewer can understand it locally.

Default rules:

- Keep functions small and single-purpose.
- Prefer early returns over deep nesting.
- Use names that explain the role of a value, not just its type.
- Keep related validation close to the point of use.
- Add comments only for non-obvious invariants, ownership rules, or protocol
  details.
- Avoid hidden control flow in macros or helper layers unless the pattern is
  already established in the repo.

A short straightforward function is preferred over a generic helper that saves
three lines but hides the behavior.

## Naming conventions

The current tree is mixed. Types are mostly `PascalCase`, while functions,
variables, and members use a mixture of `snake_case`, `lowerCamelCase`, and
older utility-style names.

Do not rename existing stable APIs just to satisfy a naming preference. For new
code, use one clear default:

- types, structs, classes, enums, and enum values: `PascalCase`
- functions and methods: `snake_case`
- local variables and parameters: `snake_case`
- data members: `snake_case`
- private data members: `snake_case_`
- file-local helper functions: `snake_case`
- macros: `ALL_CAPS` only when a macro is truly required

Examples:

- `ModelDescriptor`
- `parse_tensor_output()`
- `output_tensor_count`
- `current_cycle_measurements_`

Avoid these patterns in new code:

- mixing `snake_case` and `lowerCamelCase` inside the same class
- abbreviations that hide meaning, such as `cfg`, `tmp`, `buf2`, or `res1`
- Hungarian notation and type-encoded names
- leading underscores in user-defined identifiers

For acronyms, keep names readable. Prefer `http_server`, `uuid_counter`, and
`parse_json()` over all-caps acronym chunks embedded in identifiers.

Good:

```cpp
struct TensorDescriptor {
    size_t element_count = 0;

    bool is_empty() const {
        return element_count == 0;
    }
};

class HttpSession {
  private:
    std::string client_id_;
};
```

Avoid:

```cpp
struct tensor_descriptor {
    size_t elementCount = 0;

    bool IsEmpty() const {
        return elementCount == 0;
    }
};

class HTTPSession {
  private:
    std::string ClientId;
};
```

## Modern C++ defaults

OPK builds as C++20. New code should use modern standard-library facilities
when they improve safety or clarity.

Default rules:

- Use RAII. Do not introduce owning `new` or `delete` in normal code.
- Prefer standard containers and values over manual memory management.
- Prefer `std::unique_ptr` for single ownership and `std::shared_ptr` only when
  shared lifetime is genuinely required.
- Prefer `std::span` and `std::string_view` for non-owning inputs when the
  lifetime is clear.
- Prefer `enum class` over unscoped enums.
- Mark overridden virtual functions with `override`.
- Mark return values `[[nodiscard]]` when dropping them is likely a bug.
- Prefer `const` for values that do not change after initialization.
- Prefer direct or brace initialization when it improves clarity and prevents
  narrowing.
- Use explicit casts sparingly and only when the conversion is intentional and
  safe.
- Prefer `using` over `typedef` for new aliases.

Avoid introducing exception-driven control flow into normal runtime paths unless
the surrounding component already depends on it.

## Error handling

Use the repo's explicit result patterns for fallible work.

Default rules:

- Prefer `opk::Result` and `tl::expected` style returns for operations that can
  fail in normal runtime flow.
- Preserve useful context in error messages, especially file names, model
  names, and parser details.
- Do not swallow parse, I/O, or protocol errors without recording why the
  failure was ignored.
- If a fallback is intentional, make the fallback visible in the code and easy
  to review.

## Bounds, integers, and conversions

Many trivial runtime bugs come from unchecked indexing and implicit
conversions.

Required rules:

- Validate container size before indexing.
- Prefer `.at()` when the bounds are uncertain and the access path is not
  proven locally.
- Do not mix signed and unsigned values casually.
- Use fixed-width integer types from `<cstdint>` at protocol, file, network,
  serialization, and tensor boundaries.
- Do not introduce plain `int`, `unsigned`, or `unsigned int` when the width
  matters.
- `size_t` is appropriate for container sizes, byte counts, and indexing into
  memory-sized objects.
- Check narrowing conversions explicitly when moving between `size_t`, `int`,
  tensor dimensions, and pixel coordinates.
- Do not rely on implicit boolean or integer conversions when the meaning is
  ambiguous.

Use plain `int` only when the API is naturally signed, width does not matter,
and the surrounding interface already uses `int`. A typical example is adapting
to third-party APIs that require `int` dimensions or status codes.

Good:

```cpp
std::uint32_t frame_width = header.frame_width;
std::uint64_t timestamp_ns = metadata.timestamp_ns;
size_t tensor_count = output_tensors.size();

for (size_t tensor_index = 0; tensor_index < tensor_count; ++tensor_index) {
    // ...
}
```

Acceptable at an API boundary:

```cpp
int rc = third_party_call(handle, static_cast<int>(tensor_count));
if (rc < 0) {
    return tl::unexpected(Error(ErrorFlag::InvalidData, "third_party_call failed"));
}
```

Avoid:

```cpp
unsigned int frameWidth = header.frame_width;
int timestamp = metadata.timestamp_ns;
int tensorCount = output_tensors.size();
```

## Strings, formatting, and logging

Prefer checked formatting and repo helpers over ad hoc string handling.

Default rules:

- Use the existing `fmt`-based helpers and OPK logging wrappers where
  available.
- Prefer compile-time checked format strings for normal log messages.
- Prefer `std::string_view` for read-only string inputs when ownership is not
  needed.
- Keep log messages actionable and avoid noisy repeated logs in hot paths.

Use `opk::log::debug()`, `opk::log::info()`, `opk::log::notice()`,
`opk::log::warning()`, or `opk::log::error()` for normal severity-filtered
logging. These compile-time-check their format strings and enqueue messages
for the logging worker. `debug()` also adds the source file and line plus a
trailing newline.

Use `opk::log::instantInfo()` and `opk::log::instantError()` only when output
must be synchronous, unconditional, and directed explicitly to stdout or
stderr. Typical cases are terminal interaction and exceptional diagnostics
inside a log target. These functions bypass severity filtering and configured
targets, so they are not a replacement for normal logging.

Log target implementations must not call the asynchronous logging functions,
`opk::log::flush()`, or the target-state API. Doing so can recurse into
the logger or deadlock. A target may use `opk::log::instantInfo()` or
`opk::log::instantError()` for an exceptional internal diagnostic, but not to
deliver ordinary records.

Select the raw file target with `OPK_LOG_TARGETS=file` and configure its path
with `OPK_LOG_FILE`. Setting the path alone does not enable file logging. File
messages are appended exactly as supplied, so include any required line ending
in non-Debug messages. Debug messages already include one.

Avoid large messages and repeated per-frame or per-object messages in hot
paths. The logging queue is deliberately bounded and drops the oldest queued
record during overload. Do not add hidden unbounded queues in log targets.
See [Logging](concepts/logging.md) for the public API, configuration, and
target extension rules.

## Concurrency and lifetime

Code that starts work must also define how that work stops.

Default rules:

- Make ownership and thread lifetime obvious.
- Ensure threads are stopped and joined deterministically.
- Keep lock scope as small as practical.
- Do not let views, pointers, or references outlive the storage they refer to.
- Document non-obvious lifetime assumptions at the declaration site.

## `static`, globals, and storage duration

Prefer the narrowest practical visibility and lifetime.

Default rules:

- Use `static` only when the storage duration or linkage is intentionally part
  of the design.
- Prefer namespace-scope `static` or unnamed-namespace helpers only for
  translation-unit-local implementation details.
- Prefer class `static` members only for data and functions that are truly
  shared by all instances.
- Do not hide mutable global state behind file-local `static` objects unless
  there is no cleaner owner.
- If shared process-wide state is unavoidable, document the lifetime,
  initialization order, and synchronization assumptions.

As a default, data should have an obvious owner instead of living in
file-scope mutable state.

Good:

```cpp
namespace {

constexpr std::size_t k_default_queue_depth = 8;

bool is_valid_port(std::uint16_t port) {
    return port != 0;
}

} // namespace
```

Acceptable shared state when ownership is explicit:

```cpp
class UuidGenerator {
  public:
    static std::uint64_t next() noexcept {
        return counter_.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    static std::atomic<std::uint64_t> counter_;
};
```

Avoid:

```cpp
static std::string global_config_path;
static bool initialized = false;
```

## `constexpr` and macros

Prefer language features over preprocessor substitutions.

Default rules:

- Use `constexpr` for compile-time constants and small pure helper functions.
- Use `constinit` only when static initialization order needs to be controlled
  and the codebase already supports that usage cleanly.
- Prefer `enum class` or `constexpr` constants over `#define` integer or string
  constants.
- Prefer inline functions, templates, or lambdas over function-like macros.
- Keep macros only for the cases where the preprocessor is actually required,
  such as include guards, controlled platform feature switches, and carefully
  reviewed tracing or compatibility shims.

If a macro remains, keep it small, obvious, and isolated. Do not use macros to
hide types, ownership, or control flow.

Good:

```cpp
constexpr std::uint32_t k_magic_header = 0x50454b31u;

constexpr bool is_even(size_t value) {
    return (value % 2u) == 0u;
}

enum class ParserKind {
    Yolo,
    Classification,
};
```

Avoid:

```cpp
#define MAGIC_HEADER 0x50454b31
#define IS_EVEN(x) ((x) % 2)
#define PARSER_YOLO 1
```

## `clang-tidy` baseline

The repo-root `.clang-tidy` enforces only a focused subset of these rules.

It is intended to catch routine defects such as:

- analyzer-detected correctness issues
- suspicious bug-prone constructs
- accidental use of `NULL`
- avoidable range-copy overhead
- uninitialized variables
- narrowing conversions

This baseline is intentionally conservative. It should help reviewers catch
real mistakes without creating a large backlog of stylistic noise.

Naming checks are intentionally not enabled yet. The current repository uses
more than one naming style, so enabling `readability-identifier-naming`
immediately would create noise instead of useful review feedback.

If a warning must be suppressed, keep the suppression local and explain why the
code is safe as written.

## What should you have at the end of this document?

By the end of this page, you should have:

- a practical rule set for writing new OPK C++ code
- a clear bias toward explicit validation and explicit ownership
- a shared baseline that humans and tooling can check consistently

Success looks like this: new runtime code is easier to review, boundary checks
are obvious, and the most common trivial C++ mistakes are either prevented by
design or caught automatically.

[Back to README](index.md)
