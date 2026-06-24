# pipeline-exec

`pipeline-exec` is a proof-of-concept application for embedding a PEK GStreamer
pipeline through the public C++ `pek::runtime::Pipeline` wrapper.

It accepts one PEK pipeline JSON file, loads the `pipeline` definition from that
file, starts the pipeline, prints a small summary for every serialized Perception JSON callback,
and uses its own condition variable to react to EOS or error callbacks.

The source intentionally includes only `runtime/Pipeline.h` from the runtime API.
GStreamer types and the C++ `pek::Perception` type stay hidden behind the wrapper,
and the example keeps control of its own thread instead of calling `Pipeline::wait()`.

## Build

```sh
./examples/pipeline-exec/build.sh
```

The script rebuilds the main development tree, builds the standalone example,
and copies the binary to `examples/bin/pipeline-exec`.

## Run

```sh
./examples/bin/pipeline-exec /work/config/pipelines/debug/video.json
```

If PEK plugins are not installed globally, the example scans
`/work/development/build/meson-out` by default. Override that with
`PEK_PLUGIN_PATH` when needed:

```sh
PEK_PLUGIN_PATH=/path/to/plugins ./examples/bin/pipeline-exec /work/config/pipelines/debug/video.json
```
