<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# YOLO-E v8s promptable segmentation: local prototype

This model-local integration accepts continuing text category descriptions or
owned RGB reference images with normalized boxes. It uses one dynamically
conditioned detector for both modes. It has no browser transport or UI.

This is a locally materialized prototype, not a published model release. The
standard release model allowlist does not include it. Source and model licensing
review remains user-owned and unresolved; these files do not claim approval or
authorize publication. The author YOLO-E source/checkpoint identify AGPL-3.0;
MobileCLIP source is MIT, while the exact Apple checkpoint terms require separate
review. The derived tokenizer retains the OpenCLIP MIT notice in `scripts/`.

## Materialize and verify

Use the normal OPK development container with `docker run --network=host`
(and `docker build --network=host` when building it). Reuse an existing container
only after verifying `docker inspect --format {{.HostConfig.NetworkMode}} NAME`
reports `host`. The export tool is
qualified on Linux x86_64 with Python 3.13; its heavy PyTorch dependencies belong
in a separate export environment, not the native runtime environment.

From the repository root inside that container (substitute the active native
build directory if your build uses a different location):

```sh
./scripts/build.sh debug true
python scripts/download-models.py --models-dir config/models --model yoloe-v8s-onnx
python -m venv .cache/yoloe-export
.cache/yoloe-export/bin/pip install -r tools/yoloe/requirements.txt
.cache/yoloe-export/bin/python tools/yoloe/export.py
.cache/yoloe-export/bin/python tools/yoloe/qualify_native.py --output .cache/yoloe-native
./scripts/setup-python-ops-runtime.sh --venv .cache/yoloe-runtime --perception-sdk generated/open_perception_kit/python
OPK_QUERY_TEST_ASSETS="$PWD/.cache/yoloe-native" OPK_PYTHON_RUNTIME_VENV="$PWD/.cache/yoloe-runtime" meson test -C development/build query_model_tests prompt_inference_tests --print-errorlogs
python tools/yoloe/package.py --output .cache/yoloe-model
```

`download-models.py` owns every remote model/source/vocabulary download. Export
and package tools only consume verified local files. The asset manifest pins
THU-MIG/yoloe commit `40cd606cabdbe2b566d6f14a6b162c89206e9a1b`,
`jameslahm/yoloe` revision `b584da188a198a2e6aa0e013d3fef6d55b212603`,
`yoloe-v8s-seg.pt`, Apple `mobileclip_blt.pt`, and the OpenCLIP 2.30.0 vocabulary
with SHA-256 digests. Export receipts include source/weight hashes, wrapper and
script hashes, opset/settings, tool versions, and resulting graph hashes.
`export-report.record` is written only after all author comparisons succeed.

The local package directory contains the actual graphs, relative descriptors,
required tokenizer script/vocabulary/notice, receipts and this document. It
requires the OPK runtime and its dependency environment, not the source export
checkpoint files. No unpublished remote graph URL is assumed.

## Native command demo

Build `query-prompts` with the normal native build. Set `GST_PLUGIN_PATH` and
`LD_LIBRARY_PATH` to the native build output and ONNX Runtime library directory,
and `OPK_PYTHON_RUNTIME_VENV` to the owner-created runtime environment. For the
flat development build used during qualification:

```sh
export GST_PLUGIN_PATH="$PWD/development/build/meson-out"
export LD_LIBRARY_PATH="$GST_PLUGIN_PATH:/opt/opk-deps/onnxruntime/lib"
export OPK_PYTHON_RUNTIME_VENV="$PWD/.cache/yoloe-runtime"
development/build/meson-out/query-prompts .cache/yoloe-model target.jpg text person bus dog
development/build/meson-out/query-prompts .cache/yoloe-model target.jpg visual reference.jpg 0.05 0.35 0.30 0.85
```

Text arguments submit successive category revisions on identical target media.
Visual arguments are repeated `REFERENCE LEFT TOP RIGHT BOTTOM` groups in
normalized reference coordinates; the demo submits the groups then reverses their
order. It prints explicit object/group matches, repeats each revision, cancels it,
ends the session, and verifies subsequent frames have no stale association.
The demo deliberately limits separate references to four; the underlying protocol
supports up to sixteen groups sharing up to four references.

Camera presets are `yoloe-v8s-text-prompts` and `yoloe-v8s-visual-prompts`. They
require a native host application to send the same `opk-prompt-command-v1` events;
without a prompt the model skips inference. They do not implement an editor.

## Qualified contracts and ownership

- `text-encoder.onnx`: Int64 `[Q,77]` tokens to final Float32 `[1,Q,512]` conditioning.
  MobileCLIP normalization, learned RepRTA projection and final normalization are
  inside this graph. The detector must not apply RepRTA a second time.
- `visual-encoder.onnx`: RGB Float32 `[1,3,640,640]` full reference and Float32
  `[1,Q,80,80]` region masks to `[1,Q,512]` SAVPE conditioning. Shared-reference
  groups are encoded together, then restored to request order across references.
- `detector.onnx`: RGB Float32 `[1,3,640,640]` target and `[1,Q,512]` conditioning;
  output `[1,36+Q,8400]` predictions and `[1,32,160,160]` prototypes. Q1,2,4,16
  have actual export/runtime evidence. There is no fused fixed-query graph.

`GenericImagePreprocess (bilinear)` preserves full-image context using OPK uint8 bilinear resizing,
RGB CHW /255 and padding 114. Visual boxes follow the author's Float32 coordinate
conversion into the 80x80 grid. A box selecting no coarse cell fails explicitly.
The parser uses class-aware NMS, confidence 0.25/IoU 0.7/max 100, threshold-at640
masks, padding removal and nearest-neighbor restoration to source resolution.
This mask restoration policy is explicit and covered by original-resolution
native/author comparisons; it is not a claim that every author's rendering path
uses the same resize policy.

Text cleaning/BPE matches OpenCLIP 2.30.0, including Unicode repair and HTML
unescaping. More than 75 content tokens fails instead of silent truncation. Runtime
ftfy/regex/wcwidth wheels and both host-architecture hashes are owned by
`development/ops-python/runtime.json`; the normal runtime initializer and Debian
post-install path consume those locks. Native image and mask resizing use OPK
helpers without an OpenCV dependency.
OpenCV remains confined to the isolated upstream export/qualification environment.

Encoder and detector sessions use the same native OPK ONNX backend; prompt
preparation owns only the tokenizer/reference preparation and revision cache.
Preparation is synchronous before the frame controller. One immutable prepared
revision is cached: repeated frames reuse it; A→B→A re-encodes A. A failed update
publishes no partial conditioning. A paired old buffer may complete after cancel
or session handover and keeps its original identity. Runtime recreation invalidates
learned state. Query results carry `PromptAssociation.matches` for boxes and masks,
including an explicit successful empty association for zero detections; query
indices are never advertised as persistent class IDs.

Initial host safety limits are 16 groups, 4 references, 2048 pixels per reference
axis, 16 MiB aggregate RGB bytes and 64 MiB retained prompt ownership. The tokenizer
BPE cache is bounded at 1024 entries. These are conservative host safety bounds;
Pi performance/resource tuning is deferred. Gate0's 2,293,644 KiB peak RSS includes
PyTorch export/oracle and multiple ORT sessions and is not native steady-state
memory. Encoder replacement requires checkpoint/projection/normalization,
tokenizer and embedding-space requalification; it is not arbitrary interchange.

The native Ops are `YoloePromptPrepare`, `YoloePromptPack` and
`YoloeInstancePostprocess` in `opk-python-ops`. They use the shared
[native visual prompt adapter lifecycle](../../../docs/arch/prompt-adapters.md);
model-specific tensor semantics stay in the YOLO-E adapters. Python BYOM input
adapters are a future extension, not provided by this integration.
