# Known Limitations

This project is intended for controlled internal SDK generation and integration.
It is ready to use with owned schemas and bounded inputs, but the following
limitations should remain explicit.

## Scope

- The generator is designed for internal build pipelines, not as a public schema
  registry or public package publishing system.
- Schemas are expected to be owned or reviewed by the team using the generator.
- Generated SDKs should be produced in CI or during the consuming project's build
  process and distributed together with the binaries that use them.

## Compatibility

- Known payload ids are deterministic and content-derived, but they are still
  truncated hash values. Collisions are checked inside one generated schema set;
  they are not mathematically impossible.
- The producer schema-set SHA-256 is an exact provenance check, not a global
  compatibility decision. Compatibility is decided per payload entry:
  decodable known payloads are exposed through typed APIs, while unknown,
  changed, or malformed same-id payloads are preserved for forwarding.
- The payload-id algorithm includes a compatibility salt. Changing it is a
  deliberate wire-compatibility break.

## Runtime Behavior

- The envelope checks the outer wire container first. C++ uses
  `flatbuffers::Verifier` for the outer envelope and for typed payload blobs.
  Rust uses the generated FlatBuffers verifier for both. Python and TypeScript
  check file identifiers and parse through generated accessors, but they do not
  provide verifier-equivalent structural validation.
- C++, Python, and TypeScript check individual payload blobs when a typed API
  asks for that payload type. Rust decodes known payloads while decoding the
  envelope and preserves failed payloads with diagnostic errors.
- Typed `count`, `contains`, `get`, and `for_each` report only entries that
  successfully decode as the requested payload type.
- Unknown or incompatible entries in the known generated-id domain are preserved
  for forwarding. C++, Python, and TypeScript keep them private; Rust exposes
  read-only id/blob/error views.
- External payload APIs expose caller-owned opaque blobs by deterministic key
  handle. C++ envelope methods accept `external_key_t`; Python, Rust, and TypeScript
  external methods accept `ExternalKey`. External blob contents are
  never interpreted or verified by the SDK. The original key string is not
  stored in the envelope, and bare strings or numeric external ids are not
  accepted by public APIs.
- C++ stores newly added known payloads as native object API values and serializes
  them when the envelope is serialized. Payloads loaded from a packet are decoded
  lazily and cached on first successful typed read.
- C++ `get<T>()` returns a read-only `payload_ref<T>` handle instead of copying
  the native object. `for_each<T>()` invokes callbacks with `const T&`.
- C++ operations are protected by an internal mutex, but callers must still keep
  the envelope object alive while any thread or async task may use it. Copy and
  move operations require exclusive access to their source and destination
  envelopes; move construction and move assignment are `noexcept`.
- The optional C++ Python bridge is for embedded CPython hosts. It wraps a
  host-owned C++ envelope temporarily; the host must keep the envelope alive,
  scope the live wrapper, and ensure Python import paths include the generated
  SDK and FlatBuffers runtime. The generated `guest.pyi` supports editor analysis
  only; importing `<sdk>.guest` still requires the host-registered bridge module.
- Importing `<sdk>.guest` requires the generated `<sdk>_bridge` module to have
  been registered by the embedding host. Endpoint-only Python environments use
  `<sdk>.packet` and do not require the bridge.
- C++ Python bridge known reads return read-only proxies. They do not allow
  mutation of existing entries. Known appends pack and verify one Python
  object-api payload before adding a new C++ native entry.
- Python and TypeScript envelopes are not designed for concurrent shared
  mutation/read access.

## Resource Limits

- The SDKs do not currently enforce configurable maximum envelope size, payload
  count, or payload blob size. Consumers should apply practical limits at the
  transport or pipeline boundary.
- Python and TypeScript should be used with controlled or bounded inputs unless
  additional boundary validation is added around packet ingestion.
- Very large envelopes can cause repeated work in Python and TypeScript because
  typed counting verifies matching payload blobs.

## Generated Code

- Python FlatBuffers modules are generated under `<sdk>.fb.<schema namespace>`.
  The generator rewrites FlatBuffers' Python imports to keep schema modules
  isolated from SDK helper modules.
- The namespace `<sdk>.internalfb` is reserved for the generated envelope wire
  schema.
- The SDK name must be a portable package/namespace identifier and must not be a
  reserved keyword in the generated target languages.

## Operational Expectations

- Generated SDK releases require an explicit stable `MAJOR.MINOR.PATCH` value.
  This is artifact metadata, not an envelope or payload compatibility field.
  The generator does not compare previous releases or enforce the correct
  semantic-version bump.
- Each `flowdata-manifest.json` describes one language-generation invocation,
  not a combined multi-language release artifact. Project release tooling is
  responsible for combining manifests and packaging outputs.
- Generated file hashes describe files before any consumer-owned decoration or
  formatting. Such postprocessing invalidates those hashes, so the consuming
  project must produce final release checksums after its transformations.
- Run `./tests/sanity_check.py --keep-generated` in CI for this repository or an
  equivalent end-to-end check in the consuming project.
- Add integration tests in the consuming project that generate the SDK, build the
  relevant modules, pass a real envelope through the pipeline, and verify typed
  payload access after serialization/deserialization.
