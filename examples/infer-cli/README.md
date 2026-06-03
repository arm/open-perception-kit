# infer-cli

<<<<<<< HEAD:examples/infer-cli/README.md
`infer-cli` is a proof-of-concept command-line application for running PEK-style
media input outside GStreamer.
=======
`opchain-exec` is a proof-of-concept command-line application for running one PEK
OpChain on one image through the public C++ runtime API.
>>>>>>> abf198c (runtime api):examples/opchain-exec/README.md

The current version loads a PNG/JPEG image file with `pek::runtime::Tools`, wraps
the decoded BGRA pixels as a `pek::runtime::VideoFrame`, executes an OpChain JSON
file through `pek::runtime::OpChain`, and prints the serialized Perception JSON to stdout.
The source intentionally avoids direct `op/`, `mediaio/`, and `pek/Perception` headers.

## Build

Build the main PEK development tree first so `pek-runtime.so`, `libcommon.so`, and the
Op plugins exist:

```sh
./scripts/build-elements.sh debug true
```

Then build the example:

```sh
meson setup /tmp/infer-cli-build examples/infer-cli
meson compile -C /tmp/infer-cli-build
```

To rebuild the main development tree, rebuild `infer-cli`, and copy the binary to
`examples/bin/infer-cli`:

```sh
./examples/infer-cli/build.sh
```

## Run

```sh
/tmp/infer-cli-build/infer-cli /work/config/models/ultraface/opchain.json /work/data/images/my-image.jpg
```

Or, after running the build script:

```sh
./examples/bin/infer-cli /work/config/models/ultraface/opchain.json /work/data/images/my-image.jpg
```

The first argument is the OpChain JSON file. The second argument is the input
image file. Supported image extensions are `.png`, `.jpg`, and `.jpeg`.

<<<<<<< HEAD:examples/infer-cli/README.md
`infer-cli` prints the serialized `Perception` JSON after the OpChain finishes.
=======
`opchain-exec` prints serialized Perception JSON after the OpChain finishes.
>>>>>>> abf198c (runtime api):examples/opchain-exec/README.md
Runtime components may also print diagnostics while the chain is being built or
executed; this example intentionally keeps that behavior visible so the source
stays focused on the public runtime integration flow.
