# infer-cli

`infer-cli` is a proof-of-concept command-line application for running PEK-style
media input outside GStreamer.

The current version loads a PNG/JPEG image file with `pek::Tools`, wraps the
decoded BGRA pixels as a `pek::mediaio::PixelBufferVideoFrame`, executes an OpChain
JSON file, and prints the serialized `Perception` result to stdout.

## Build

Build the main PEK development tree first so `libcommon.so` and the Op plugins
exist:

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

`infer-cli` prints the serialized `Perception` JSON after the OpChain finishes.
Runtime components may also print diagnostics while the chain is being built or
executed; this example intentionally keeps that behavior visible so the source
stays focused on the basic PEK integration flow.
