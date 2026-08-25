---
sidebar_position: 8
sidebar_label: Python script Op
---

# Run a stateful Python script in an OpChain

Use `pek-python-ops/PythonScript` when a trusted Python script needs to inspect
inference output tensors or append schema-defined data to `FrameResults`. The
script runs inside the pipeline process and is loaded once for each Op instance,
so module globals persist between calls.

Python postprocessors are supported when a native PEK pipeline runs inside the
official quick-start or deployment container. Both images supply the compatible
CPython interpreter, NumPy, FlatBuffers runtime, and generated Perception guest
bridge; no Python installation from the host system is used.

## Configure the Op

Add the operation at the required position in an OpChain:

```json
{
  "id": "pek-python-ops/PythonScript",
  "instanceId": "python-classifier",
  "attributes": {
    "script": "scripts/process.py",
    "pythonPaths": ["scripts/modules"]
  }
}
```

Relative `script` and `pythonPaths` values resolve from the directory containing
the OpChain descriptor. Restart the pipeline after changing a script.
`instanceId` is optional, but assigning one gives payloads a stable producer
identity even if the operation order changes.

Place the operation after inference to receive output tensors. It may also be
used elsewhere in the chain, in which case `tensors` is empty when no inference
outputs are available.

## Implement the script

```python
import numpy

from perception.guest import Envelope
from pek_python_ops import Context, Tensor


frame_count = 0


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

`process` must accept the envelope, tensor tuple, and call-scoped context and
return `None`. Existing FrameResults payloads are read-only bridge proxies. Use
`env.add(...)` with the generated object API to append new payloads, following
the same pattern as a generated Perception SDK consumer.

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
`pek-python-ops/PythonScript` component, and the script filename. The context
property is read-only. Do not mutate its producer object; use it while
constructing payloads for the current invocation.

## Tensor contract

Each `pek_python_ops.Tensor` provides:

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

## Run the MobileNet demonstration

The checked-in MobileNet example continuously classifies a bundled real sample
image and places a Python operation between inference and the standard ImageNet
postprocessor. Python reads the output tensor and
independently calculates its top five classifications while tracking how many
consecutive frames retain the same top class. The Python results appear in the
lower-right corner, while the standard C++ top classifications remain in the
lower-left for comparison. Both lists use the same five-row rank, label, and
confidence format and align vertically. The WebUI labels each list with its
recorded producer implementation: the C++ parser name on the left and the
Python script filename on the right. Native `pekosd` drawing is disabled in this
pipeline so the browser does not render metadata on top of labels already burned
into the video. The stable-frame count is retained in the Python layer's `tags`
metadata instead of changing the visible label. The
model-local Python demo bundles the same 1,001 ImageNet labels used by the C++
parser because it runs before postprocessing.

The pipeline also includes `pekperformance` and `pekcomm`. The performance
overlay reports the average and p95 duration of the complete Python operation,
including tensor wrapping, the Python call, and FrameResults updates. Its video
is capped at 720p and 20 FPS to keep the embedded Python demonstration
responsive and visually smooth. `imagefreeze` keeps the real sample live without
end-of-stream pipeline restarts, so Python state persists until the user stops
the pipeline.

Build with Python operations enabled and run the pipeline:

```bash
PEK_PYTHON_OPS=enabled ./scripts/build-elements.sh debug true
./tools/pek-menu mobilenet-python-classification
```

The implementation is in
`config/models/mobilenetv2/scripts/python_classification.py`, and its operation
order is documented by
`config/models/mobilenetv2/opchain-python-classification.json`. The stable-frame count
resets when the predicted class changes or the pipeline is recreated.

## Failure behavior

PEK loads and validates the script while configuring the OpChain, before the
pipeline starts processing frames:

1. The `script` file and every configured `pythonPaths` directory must exist.
2. PEK compiles the complete script. Invalid Python syntax fails configuration
   with the script path, line number, offending source line, and `SyntaxError`
   details supplied by Python.
3. PEK evaluates the module once. Missing imports and exceptions raised by
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
must return `None`. In either case, PEK aborts the current OpChain execution,
posts a GStreamer element error, and stops the pipeline instead of silently
dropping the affected frame or continuing with the next one. Catch and handle
an exception inside the script only when continuing is intentional and safe.

Detailed errors are currently available in the native launcher or container
logs. The WebUI does not display Python tracebacks; it may only show the visible
effect of the failed pipeline, such as a frozen or disconnected stream.

## State and deployment

- Module globals persist for the lifetime of the OpChain and reset when the
  pipeline recreates it. In a looped OpChain, state advances once per Op
  invocation rather than once per input frame.
- Imported helper modules use Python's process-wide `sys.modules` cache. Keep
  isolated mutable state in the main script module.
- Scripts are not sandboxed. They can access the process, filesystem, network,
  and imported native modules. A slow script blocks the streaming thread.
- Build with `-Dpython_ops=enabled`, or set `PEK_PYTHON_OPS=enabled` when using
  `scripts/build-elements.sh`. Run the resulting pipeline through a native PEK
  launcher inside an official PEK container. Python-hosted GStreamer
  applications, standalone binary archives, and deployments outside those
  containers are not part of the current support contract.
