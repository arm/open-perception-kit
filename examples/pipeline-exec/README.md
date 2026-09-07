# pipeline-exec

`pipeline-exec` is a proof-of-concept application for embedding a PEK GStreamer
pipeline through the public C++ `pek::runtime::Pipeline` wrapper, then consuming
FrameResults packets with the generated Perception C++ SDK.

It accepts one PEK pipeline JSON file, loads the `pipeline` definition from that
file, starts the pipeline, prints a terminal-friendly dump for every serialized
FrameResults packet callback, and uses its own condition variable to react to
EOS or error callbacks. The example can also enable PEK historical performance
capture and write completed spans to CSV through the public runtime
`PerformanceMetrics` facade.

The source intentionally keeps GStreamer and internal `pek/` implementation
types behind `pek::runtime::Pipeline`, but it does include generated
`perception::metadata::*` payload types because typed result consumption is part
of the example. Each packet is validated through the example-local
`PerceptionPacket` helper, visited with typed Perception SDK lambdas, and
displayed through the example-local `TextDisplay` helper. Unknown payload types
are reported as `Unknown payload type`.

## Build

```sh
./examples/pipeline-exec/build.sh
```

The script rebuilds the main development tree, builds the standalone example,
and copies the binary to `examples/bin/pipeline-exec`. The standalone example
build also needs a compatible FlatBuffers C++ package visible to Meson because
it includes generated Perception SDK headers.

## Run

Set the project root when running directly from a checkout outside the
container:

```sh
export PEK_PROJECT_ROOT="$(pwd -P)"
```

```sh
./examples/bin/pipeline-exec \
  "$PEK_PROJECT_ROOT/config/pipelines/debug/video.json"
```

To collect performance spans:

```sh
./examples/bin/pipeline-exec \
  --perf-csv "$PEK_PROJECT_ROOT/var/pipeline-perf.csv" \
  "$PEK_PROJECT_ROOT/config/pipelines/debug/video.json"
```

The CSV is written by `pek::runtime::PerformanceMetrics::writeCsv()`. It
contains completed historical spans only; scopes still open when the process
finishes are omitted rather than assigned a fabricated end time.

If PEK plugins are not installed globally, the example scans
`${PEK_PROJECT_ROOT:-/work}/development/build-active/meson-out` by default.
`build-active` points to the correct container or native build directory.
Override plugin discovery with `PEK_PLUGIN_PATH` when needed:

```sh
PEK_PLUGIN_PATH=/path/to/plugins \
  ./examples/bin/pipeline-exec \
  "$PEK_PROJECT_ROOT/config/pipelines/debug/video.json"
```
