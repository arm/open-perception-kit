# pipeline-run

`pipeline-run` is a proof-of-concept application for embedding a PEK GStreamer
pipeline through the C++ `pek::api::Pipeline` facade.

It accepts one PEK pipeline JSON file, loads the `pipeline` definition from that
file, starts the pipeline, prints a small summary for every serialized Perception JSON callback,
and waits until the pipeline posts EOS or an error.

The source intentionally includes only `api/Pipeline.h` from PEK. GStreamer types,
internal PEK runtime headers, and the C++ `pek::Perception` type stay hidden
behind the API layer.

## Build

```sh
./examples/pipeline-run/build.sh
```

The script rebuilds the main development tree, builds the standalone example,
and copies the binary to `examples/bin/pipeline-run`.

## Run

```sh
./examples/bin/pipeline-run /work/config/pipelines/debug/video.json
```

If PEK plugins are not installed globally, the example scans
`/work/development/build/meson-out` by default. Override that with
`PEK_PLUGIN_PATH` when needed:

```sh
PEK_PLUGIN_PATH=/path/to/plugins ./examples/bin/pipeline-run /work/config/pipelines/debug/video.json
```
