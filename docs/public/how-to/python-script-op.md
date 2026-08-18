---
sidebar_position: 8
sidebar_label: Python script Op
---

# Run a stateful Python script in an OpChain

Use `pek-python-ops/PythonScript` when a trusted Python script needs to inspect
inference output tensors or append schema-defined data to `FrameResults`. The
script runs inside the pipeline process and is loaded once for each Op instance,
so module globals persist between calls.

## Configure the Op

Add the operation at the required position in an OpChain:

```json
{
  "id": "pek-python-ops/PythonScript",
  "attributes": {
    "script": "scripts/process.py",
    "pythonPaths": ["scripts/modules"]
  }
}
```

Relative `script` and `pythonPaths` values resolve from the directory containing
the OpChain descriptor. Restart the pipeline after changing a script.

Place the operation after inference to receive output tensors. It may also be
used elsewhere in the chain, in which case `tensors` is empty when no inference
outputs are available.

## Implement the script

```python
import numpy

from perception.guest import Envelope
from pek_python_ops import Tensor


frame_count = 0


def process(env: Envelope, tensors: tuple[Tensor, ...]) -> None:
    global frame_count
    frame_count += 1

    if tensors:
        output = tensors[0]
        values = output.array
        if output.quantized:
            values = (values.astype(numpy.float32) - output.zero_point) * output.scale

        print(frame_count, output.index, output.name, values.shape, values.dtype)
```

`process` must accept the envelope and tensor tuple and return `None`. Existing
FrameResults payloads are read-only bridge proxies. Use `env.add(...)` with the
generated object API to append new payloads, following the same pattern as a
generated Perception SDK consumer.

## Tensor contract

Each `pek_python_ops.Tensor` provides:

- `index`: output position in the model
- `name`: model output name when an upstream inference Op is available
- `array`: C-contiguous, read-only NumPy array over the backend output memory
- `scale` and `zero_point`: quantization parameters
- `quantized`: whether integer dequantization applies

The arrays support `uint8`, `int8`, `float16`, `float32`, and `int64` outputs.
They are zero-copy views valid only while `process` is running. Never retain the
array, the envelope, or bridge-backed payload proxies in module state. Retain a
tensor value only by making an explicit copy:

```python
saved_output = tensors[0].array.copy()
```

Python cannot invalidate a retained NumPy view after the call. Such a view may
observe memory reused by the next inference and eventually become unsafe.

## State, errors, and deployment

- Module globals persist for the lifetime of the OpChain and reset when the
  pipeline recreates it. In a looped OpChain, state advances once per Op
  invocation rather than once per input frame.
- Imported helper modules use Python's process-wide `sys.modules` cache. Keep
  isolated mutable state in the main script module.
- An uncaught Python exception fails processing and includes its traceback in
  the runtime error.
- Scripts are not sandboxed. They can access the process, filesystem, network,
  and imported native modules. A slow script blocks the streaming thread.
- Build with `-Dpython_ops=enabled`, or set `PEK_PYTHON_OPS=enabled` when using
  `scripts/build-elements.sh`. Runtime installations require a compatible
  CPython ABI. PEK release packages include pinned NumPy and FlatBuffers Python
  runtimes together with the generated `perception` package.
