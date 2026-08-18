# opchain-exec

`opchain-exec` is a proof-of-concept command-line application for running one PEK
OpChain on one image through the public C++ runtime API.

The current version loads a PNG/JPEG image file with `pek::runtime::Tools`, wraps
the decoded BGRA pixels as a `pek::runtime::VideoFrame`, executes an OpChain JSON
file through `pek::runtime::OpChain`, and prints the serialized Perception JSON to stdout.
The source intentionally avoids direct `op/`, `mediaio/`, and `pek/Perception` headers.

## Build

Build the main PEK development tree first so `pek-runtime.so`, `libpek-common.so`, and the
Op plugins exist:

```sh
./scripts/build.sh debug true
```

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
/tmp/opchain-exec-build/opchain-exec /work/config/models/ultraface/opchain.json /work/data/images/my-image.jpg
```

Or, after running the build script:

```sh
./examples/bin/opchain-exec /work/config/models/ultraface/opchain.json /work/data/images/my-image.jpg
```

The first argument is the OpChain JSON file. The second argument is the input
image file. Supported image extensions are `.png`, `.jpg`, and `.jpeg`.

`opchain-exec` prints serialized Perception JSON after the OpChain finishes.
Runtime components may also print diagnostics while the chain is being built or
executed; this example intentionally keeps that behavior visible so the source
stays focused on the public runtime integration flow.
