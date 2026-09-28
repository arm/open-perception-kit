---
sidebar_position: 9
sidebar_label: Python script Op
---

# Run a stateful Python script in an OpChain

Use `opk-python-ops/PythonScript` when a trusted Python script needs to inspect
inference output tensors or append schema-defined data to `FrameResults`. The
script runs inside the pipeline process and is loaded once for each Op instance,
so module globals persist between calls.

Python postprocessors are supported when an OPK pipeline runs inside the
official quick-start or deployment container, or from an extracted OPK binary
release on its supported Debian platform. The pipeline host may be native or
may already be running the compatible CPython interpreter supplied by the OPK
environment. The containers supply NumPy, the FlatBuffers runtime, and the
generated Open Perception Kit guest bridge; no Python installation from the development
host is used.

## Developer workflow

Start the standard OPK development environment and enter its shell from the
host:

```bash
./scripts/quick_start.sh
./scripts/enter_cli.sh
```

Build and run Python Op pipelines from that container shell. The container
already provides the locked Python runtime and sets
`OPK_PYTHON_RUNTIME_VENV`; do not create a Python Op virtual environment or
install NumPy and FlatBuffers on the host.

The script is loaded by the native `opk-python-ops` component while the pipeline
is processing frames. A native host causes the component to initialize its
locked interpreter. A Python-hosted GStreamer application instead causes the
component to acquire the GIL and attach its bridge modules to the existing
interpreter; it does not reinitialize or take ownership of that interpreter.

## Configure the Op

Add the operation at the required position in an OpChain:

```json
{
  "id": "opk-python-ops/PythonScript",
  "instanceId": "python-classifier",
  "attributes": {
    "script": "scripts/process.py",
    "pythonPaths": ["scripts/modules"]
  }
}
```

Absolute `script` and `pythonPaths` values are used unchanged. Relative values
resolve from the directory containing the inference operation's `modelDescriptor`.
An OpChain with model descriptors in multiple directories must use absolute Python
paths to avoid an ambiguous model-relative base. Restart the pipeline after
changing a script.
`instanceId` is optional, but assigning one gives payloads a stable producer
identity even if the operation order changes.

Place the operation after inference to receive output tensors. It may also be
used elsewhere in the chain, in which case `tensors` is empty when no inference
outputs are available. A PythonScript operation may be the terminal
postprocessor for an inference stage when it appends all required FrameResults
itself. It may instead appear before `opk-std-ops/GenericPostprocess` when Python
and native postprocessing both need the same inference outputs.

## Implement the script

```python
import numpy

from open_perception_kit.guest import Envelope
from opk_python_ops import Context, Tensor, python_script


frame_count = 0


@python_script
def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    global frame_count
    frame_count += 1

    if tensors:
        output = tensors[0]
        values = output.array
        if output.quantized:
            values = (values.astype(numpy.float32) - output.zero_point) * output.scale

        print(frame_count, output.index, output.name, values.shape, values.dtype)
```

The `python_script` decorator returns the original function unchanged and gives
IDEs and type checkers the reusable `ProcessCallback` contract. The decorated
`process` function must accept the envelope, tensor tuple, and call-scoped
context and return `None`. Existing FrameResults payloads are read-only bridge
proxies. Use `env.add(...)` with the generated object API to append new payloads,
following the same pattern as a generated Open Perception Kit consumer.

`context.producer_info` contains the generated `ProducerInfoT` for the current
Python Op. Attach it to the `LayerInfoT` of payloads created by the script:

```python
layer = LayerInfoT(
    model="example-model",
    contentType="classification",
    producer=context.producer_info,
)
```

The producer records the runtime instance ID, the
`opk-python-ops/PythonScript` component, and the script filename. The context
property is read-only. Do not mutate its producer object; use it while
constructing payloads for the current invocation.

## Tensor contract

Each `opk_python_ops.Tensor` provides:

- `index`: output position in the model
- `name`: model output name when an upstream inference Op is available
- `array`: C-contiguous, read-only NumPy array over the backend output memory
- `scale` and `zero_point`: quantization parameters
- `quantized`: whether integer dequantization applies

The arrays support `uint8`, `int8`, `float16`, `float32`, and `int64` outputs.
They are zero-copy views valid only while `process` is running. This is a hard
lifetime boundary, not a recommendation: never retain the array, the envelope,
or bridge-backed payload proxies in module state. Retain a tensor value only by
making an explicit copy:

```python
saved_output = tensors[0].array.copy()
```

Python cannot invalidate a retained NumPy view after the call. Such a view may
observe memory reused by the next inference and eventually become unsafe.

## Run the checked-in BlazeFace demonstration

The checked-in `development/examples/byom-blazeface` example uses
`opk-python-ops/PythonScript` after ONNX inference for model-specific
postprocessing. Build with Python operations enabled, then run it from the
official development container:

```bash
OPK_PYTHON_OPS=enabled ./scripts/build.sh debug true
cd development/examples/byom-blazeface
python3 run.py
```

Its `opchain.json` and `postprocess.py` show the supported integration pattern.

## Failure behavior

OPK loads and validates the script while configuring the OpChain, before the
pipeline starts processing frames:

1. The `script` file and every configured `pythonPaths` directory must exist.
2. OPK compiles the complete script. Invalid Python syntax fails configuration
   with the script path, line number, offending source line, and `SyntaxError`
   details supplied by Python.
3. OPK evaluates the module once. Missing imports and exceptions raised by
   module-level code fail configuration with a Python traceback.
4. The loaded module must define a callable `process` that accepts three
   positional arguments. Missing or incompatible callbacks fail configuration
   before streaming begins.

Any configuration failure prevents the pipeline from starting. The native
launcher or container log first reports the detailed Python or OpChain error,
then GStreamer reports that the OpChain could not be set up.

During streaming, each invocation must complete successfully and return
`None`. An uncaught Python exception produces a runtime error containing the
script path and complete Python traceback. Returning another value also fails
the invocation, with an error stating that `process(env, tensors, context)`
must return `None`. In either case, OPK aborts the current OpChain execution,
posts a GStreamer element error, and stops the pipeline instead of silently
dropping the affected frame or continuing with the next one. Catch and handle
an exception inside the script only when continuing is intentional and safe.

Detailed errors are currently available in the native launcher or container
logs. The WebUI does not display Python tracebacks; it may only show the visible
effect of the failed pipeline, such as a frozen or disconnected stream.

## State and deployment

### Advanced custom Linux environments

Normal quick-start users do not run `scripts/setup-python-ops-runtime.sh`. OPK
Docker builds use this initializer to assemble the locked runtime consistently.
Run it manually only when maintaining a custom OPK Linux image or reproducing
the image setup in another supported Linux environment.

The runtime requires:

- a compatible CPython interpreter with `venv` support
- Python development headers when building the native Python operation
- the architecture-specific NumPy wheel locked in
  `development/ops-python/runtime.json`
- the FlatBuffers Python runtime locked in `tools/perception/sdk.json`
- the generated Open Perception Kit Python package when scripts import `open_perception_kit`

The native `opk_python_ops` module is produced by the OPK native build; it is
not installed by pip or by this initializer. Installing the Python dependencies
alone does not create a supported standalone Python execution environment.

To create the locked runtime in such a Linux environment, run:

```bash
./scripts/setup-python-ops-runtime.sh \
  --venv .venv-python-ops \
  --perception-sdk generated/open_perception_kit/python
export OPK_PYTHON_RUNTIME_VENV="$PWD/.venv-python-ops"
```

The initializer selects the architecture-specific NumPy wheel from
`development/ops-python/runtime.json`, selects the FlatBuffers wheel from
`tools/perception/sdk.json`, verifies their checksums through pip, installs the
generated Open Perception Kit Python package when requested, and validates the installed
versions. It does not support a native macOS or Windows developer workflow.

- Module globals persist for the lifetime of the OpChain and reset when the
  pipeline recreates it. In a looped OpChain, state advances once per Op
  invocation rather than once per input frame.
- Imported helper modules use Python's process-wide `sys.modules` cache. Keep
  isolated mutable state in the main script module.
- Scripts are not sandboxed. They can access the process, filesystem, network,
  and imported native modules. A slow script blocks the streaming thread.
- Build with `-Dpython_ops=enabled`, or set `OPK_PYTHON_OPS=enabled` when using
  `scripts/build.sh`. Run the resulting pipeline inside an official OPK
  container or with the matching extracted OPK binary release on Debian Trixie.
  A Python-hosted GStreamer application must use the compatible CPython runtime
  and locked dependencies from that same environment. Other deployment
  platforms are not part of the current support contract.
