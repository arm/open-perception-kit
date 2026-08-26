# pipeline-exec

`pipeline-exec` is a proof-of-concept application for embedding a PEK GStreamer
pipeline through the public C++ `pek::runtime::Pipeline` wrapper.

It accepts one PEK pipeline JSON file, loads the `pipeline` definition from that
file, starts the pipeline, prints a small summary for every serialized
FrameResults transport callback, and uses its own condition variable to react
to EOS or error callbacks. The callback JSON identifies the transport encoding
and carries the serialized packet as base64 rather than exposing payload layers
as JSON. The example can also enable PEK historical performance capture and write
completed spans to CSV through the public runtime `PerformanceMetrics` facade.

The source intentionally uses only the public runtime API. GStreamer types and
the C++ `perception::FrameResults` type stay hidden behind the wrapper, and the
example keeps control of its own thread instead of calling `Pipeline::wait()`.

## Build

```sh
./examples/pipeline-exec/build.sh
```

The script rebuilds the main development tree, builds the standalone example,
and copies the binary to `examples/bin/pipeline-exec`.

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
