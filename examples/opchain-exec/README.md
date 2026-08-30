# opchain-exec

`opchain-exec` is a proof-of-concept command-line application for running one PEK
OpChain on one image through the public C++ runtime API, then consuming the
returned packet with the generated Perception C++ SDK.

The current version loads a PNG/JPEG image file with `pek::runtime::Tools`, wraps
the decoded BGRA pixels as a `pek::runtime::VideoFrame`, executes an OpChain JSON
file through `pek::runtime::OpChain`, validates the binary Perception packet
through the example-local `PerceptionPacket` helper, and demonstrates typed
generated Perception SDK payload handling with lambdas. The source intentionally
avoids direct `op/`, `mediaio/`, and internal `pek/Perception` headers, but it
does include generated `perception::metadata::*` payload types because typed
result consumption is part of the example.

## Build

Build the main PEK development tree first so `pek-runtime.so`, `libpek-common.so`, and the
Op plugins exist:

```sh
./scripts/build.sh debug true
```

The build script selects that build through `development/build-active`, whether
the main tree was built in Docker or natively.

Then build the example:

```sh
meson setup /tmp/opchain-exec-build examples/opchain-exec
meson compile -C /tmp/opchain-exec-build
```

To rebuild the main development tree, rebuild `opchain-exec`, and copy the binary to
`examples/bin/opchain-exec`:

```sh
./examples/opchain-exec/build.sh
```

## Run

```sh
/tmp/opchain-exec-build/opchain-exec \
  config/models/ultraface/opchain.json \
  data/images/my-image.jpg
```

Or, after running the build script:

```sh
./examples/bin/opchain-exec \
  config/models/ultraface/opchain.json \
  data/images/my-image.jpg
```

The first argument is the OpChain JSON file. The second argument is the input
image file. Supported image extensions are `.png`, `.jpg`, and `.jpeg`.

`opchain-exec` prints the Perception packet size and a terminal-friendly dump of
known FrameResults payloads after the OpChain finishes. Each typed branch uses
the example-local `TextDisplay` helper to print payload content, including
detection labels, confidence scores, object ids, and bounding boxes when present.
Unknown payload types are reported as `Unknown payload type`.

Runtime components may also print diagnostics while the chain is being built or
executed; this example intentionally keeps that behavior visible so the source
stays focused on runtime execution plus typed Perception SDK result consumption.
