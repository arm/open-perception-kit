---
name: author-opk-python-postprocessor
description: Author and integrate trusted, model-local Python postprocessors using `opk-python-ops/PythonScript`. Use when adding or modifying an OPK PythonScript Op, Python guest postprocessor, tensor-processing script, stateful Python inference callback, model opchain integration, demonstrational pipeline, or related tests and documentation. Do not use for embedded Python runtime or bridge implementation changes, external SDK consumers, or new Open Perception Kit payload schemas.
---

# Author OPK Python Postprocessor

Implement Python postprocessing through the supported model and OpChain
extension surfaces. Keep scripts synchronous, payloads schema-defined, and
validation inside the official OPK containers.

## Establish the Boundary

1. Read `docs/public/how-to/python-script-op.md` and the model descriptor and
   opchain being extended.
2. Inspect the inference output tensor names, shapes, value types, and
   quantization metadata.
3. Select an existing generated FrameResults payload type for the result.
4. Use `$evolve-perception-schema` and `$regenerate-perception-sdk` before
   continuing if no existing payload represents the required persistent data.

Do not edit generated SDK sources directly. Do not change the embedded Python
runtime or bridge unless the task explicitly targets those components.

## Author the Script

Place model-specific scripts under `config/models/<model>/scripts/` and use the
supported callback:

```python
from open_perception_kit.guest import Envelope
from opk_python_ops import Context, Tensor


def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    ...
```

- Keep `process` synchronous and return `None`.
- Validate tensor count, names, shapes, and value types before interpreting data.
- Use `tensor.scale` and `tensor.zero_point` when `tensor.quantized` is true.
- Treat `env`, payload proxies, `Tensor`, and `tensor.array` as callback-scoped.
- Copy tensor data explicitly before retaining it across frames.
- Keep intentional state in the main script module; imported helpers share the
  process-wide `sys.modules` cache.
- Attach `context.producer_info` to the emitted payload's `LayerInfoT`.
- Append generated object-API payloads with `env.add(...)`; do not create
  hand-written serializers or ad hoc result dictionaries.

Use `config/models/mobilenetv2/scripts/python_classification.py` as the primary
checked-in example.

## Integrate the OpChain

- Use string `MAJOR.MINOR.PATCH` versions for Model, OpChain, and pipeline JSON.
  Increment each edited file's patch within its current major/minor, including
  changes to script references or annotations. Start new files at `"1.0.0"`.
  A major mismatch fails; a minor mismatch warns and continues; patch is ignored
  at runtime. See `docs/public/concepts/configuration-compatibility.md`.
- Add `opk-python-ops/PythonScript` after the inference operation whose tensors
  the script consumes.
- Set a stable, descriptive `instanceId` when the script emits payloads.
- Keep `script` and `pythonPaths` relative to the opchain when possible.
- Add a top-level pipeline only when the feature needs a runnable `opk-menu`
  preset or demonstrational workflow.
- Run the script only through a native OPK pipeline in the official quick-start
  or deployment container. `open_perception_kit.guest` intentionally does not import in
  a normal standalone Python process.

## Test the Result

Add the smallest useful coverage for the change:

- a pure Python test for model-specific tensor interpretation and emitted
  payload contents
- a C++ PythonScript Op fixture for callback, state, tensor, or error-contract
  behavior
- a GStreamer pipeline test when adding a reusable demo or production opchain

Build with Python operations enabled and run focused tests first:

```bash
OPK_PYTHON_OPS=enabled ./scripts/build.sh debug true
meson test -C /work/development/build \
  python_script_op_tests \
  python_classification_pipeline_tests \
  --print-errorlogs
```

Then run the full Meson suite. For a runnable preset, also execute it through
`./tools/opk-menu <pipeline-id>` and verify the emitted FrameResults in addition
to the video presentation.

## Report

State the consumed tensors, emitted payload types, state retained across frames,
producer identity, opchain and pipeline changes, official container used, tests
run, and any accepted zero-copy or shared-interpreter limitation.
