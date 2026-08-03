# pipeline-exec

`pipeline-exec` is a proof-of-concept application for embedding a PEK GStreamer
pipeline through the public C++ `pek::runtime::Pipeline` wrapper.

It accepts one PEK pipeline JSON file, loads the `pipeline` definition from that
file, starts the pipeline, prints a small summary for every serialized
FrameResults JSON callback, and uses its own condition variable to react to EOS
or error callbacks. It can also enable PEK historical performance capture and
write completed spans to CSV through the public runtime `PerformanceMetrics`
facade.

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

```sh
./examples/bin/pipeline-exec /work/config/pipelines/debug/video.json
```

To collect performance spans:

```sh
./examples/bin/pipeline-exec \
  --perf-csv /work/var/pipeline-perf.csv \
  /work/config/pipelines/debug/video.json
```

The CSV is written by `pek::runtime::PerformanceMetrics::writeCsv()`. It
contains completed historical spans only; scopes still open when the process
finishes are omitted rather than assigned a fabricated end time.

If PEK plugins are not installed globally, the example scans
`/work/development/build/meson-out` by default. Override that with
`PEK_PLUGIN_PATH` when needed:

```sh
PEK_PLUGIN_PATH=/path/to/plugins ./examples/bin/pipeline-exec /work/config/pipelines/debug/video.json
```
