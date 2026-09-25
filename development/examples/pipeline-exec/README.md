# pipeline-exec

`pipeline-exec` is a proof-of-concept application for embedding an OPK GStreamer
pipeline through the public C++ `opk::runtime::Pipeline` wrapper, then consuming
FrameResults packets with the generated Open Perception Kit C++ SDK.

It accepts one OPK pipeline JSON file, loads the `pipeline` definition from that
file, starts the pipeline, prints a terminal-friendly dump for every serialized
FrameResults packet callback, and uses its own condition variable to react to
EOS or error callbacks. The example can also enable OPK historical performance
capture and write completed spans to CSV through the public runtime
`PerformanceMetrics` facade.

The no-argument `Pipeline::start()` overload uses `StartOptions` defaults:
`Error` logging is sent to stderr while stdout and file logging are disabled.
Applications that need different process-wide logging can populate
`StartOptions::logLevel`, `logToStdout`, `logToStderr`, and `logToFile` and call
`start(options)`.

The source intentionally keeps GStreamer and internal `opk/` implementation
types behind `opk::runtime::Pipeline`, but it does include generated
`open_perception_kit::metadata::*` payload types because typed result consumption is part
of the example. Each packet is validated through the example-local
`PerceptionPacket` helper, visited with typed Open Perception Kit lambdas, and
displayed through the example-local `TextDisplay` helper. Unknown payload types
are reported as `Unknown payload type`.

## Build

```sh
./scripts/build.sh debug true
```

The example is part of the main Meson build and is staged to
`tools/pipeline-exec` beside `tools/opk-menu`.

## Run

Set the project root when running directly from a checkout outside the
container:

```sh
export OPK_PROJECT_ROOT="$(pwd -P)"
```

```sh
./tools/pipeline-exec \
  "$OPK_PROJECT_ROOT/config/pipelines/yolo26n-320.json"
```

To collect performance spans:

```sh
./tools/pipeline-exec \
  --perf-csv "$OPK_PROJECT_ROOT/var/pipeline-perf.csv" \
  "$OPK_PROJECT_ROOT/config/pipelines/yolo26n-320.json"
```

The CSV is written by `opk::runtime::PerformanceMetrics::writeCsv()`. It
contains completed historical spans only; scopes still open when the process
finishes are omitted rather than assigned a fabricated end time.

If OPK plugins are not installed globally, the example scans
`${OPK_PROJECT_ROOT:-/work}/development/build-active/meson-out` by default.
`build-active` points to the correct container or native build directory.
Override plugin discovery with `OPK_PLUGIN_PATH` when needed:

```sh
OPK_PLUGIN_PATH=/path/to/plugins \
  ./tools/pipeline-exec \
  "$OPK_PROJECT_ROOT/config/pipelines/yolo26n-320.json"
```
