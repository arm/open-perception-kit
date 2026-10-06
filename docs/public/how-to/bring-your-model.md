---
title: Bring Your Model
sidebar_position: 4
sidebar_label: Bring Your Model
description: Add a model through descriptors, OpChains, parser selection, and pipeline presets before changing runtime code.
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->


# Bring Your Model

This page describes the shortest practical path for bringing your own model into OPK with as little runtime-code change as possible.

## What will you learn from this documentation?

If you follow this page successfully, you will learn how to add a model to OPK by reusing the existing descriptor, opchain, and parser structure wherever possible.

At the end of this page, you should have a model folder, a matching `model.json`, a working `opchain.json`, and a clear decision on whether an existing parser is enough or whether you need custom postprocessing.

## Supported model formats

The codebase currently supports these runtime/model combinations:

- ONNX Runtime with `.onnx` models
- ExecuTorch with `.pte` models

These are the implementations currently available in the tree. The OpChain v1 extension contract is
backend-independent: an exact `<library>/Inference` ID has one required `modelDescriptor` attribute,
and its runtime implementation must provide `OpInterfaceInference`. Adding a conforming backend does
not require adding its library name to the descriptor validator, but it still requires the runtime
library, factory, interface implementation, and backend-specific artifact support.

If you want the least friction, start with ONNX and reuse an existing output parser.

## Minimum files for a new model

The usual place for a new model is:

```text
config/models/<your-model>/
```

At minimum, that folder should contain:
- a model file at the descriptor's `modelFile` path when the runtime starts
- `model.json` or non-empty `model-<variant>.json` files for colocated stages
- usually `opchain.json`
- `index.md`

For most users, these files are the main integration interface of the system. The default path is to describe the model with `model.json`, connect it with `opchain.json`, and let the existing runtime elements do the rest.

For local development, the model file can live in the bind-mounted checkout.
New `.onnx` and `.pte` files are ignored by Git and the Docker build
context unless the repository explicitly allowlists them. To include a new
published model in a container image, use `hfDownload` instead of relying on a
new checked-in binary.

## Required descriptor metadata

`model.json` is the usual runtime descriptor used by the inference Op. Colocated multi-stage models
can also use `model-<variant>.json`. These names select Model validation; `opchain.json` and
`opchain-<variant>.json` select OpChain validation. Variants must be non-empty.

Typical fields are:
- `version`, initially set to the string `"1.0.0"`
- `name`
- `modelFile`
- `dynamicOutput`
- `contentType` when applicable
- `inputTensors`
- `outputTensors` when outputs are static

`modelFile` is a local filesystem path. Relative paths are resolved from the
directory containing the Model descriptor, while absolute paths are used unchanged.
Path components are not lexically rewritten, so filesystem symlink and `..`
resolution keeps its normal meaning. Prefer a relative path so the model folder
remains portable. URI values are not supported. For a published,
single-file model, add an `hfDownload` object containing the Hugging Face API's
`repo_id`, full commit `revision`, and `filename` arguments. Add `sha256` when
the artifact digest is known so the downloader verifies it before atomically
installing the model. The container build
tries to download that one artifact; the runtime does not interpret remote
locators or hold Hub credentials. Download failures are logged and skipped, so
verify that every model required by the selected pipeline is present in the
built image. Because the image stages only the model folder, `hfDownload`
requires `modelFile` to stay within its descriptor directory; use a relative
path for that combination. The downloader validates each Model descriptor it
consumes against `config/schemas/v1/model.schema.json` before starting any
remote download. Schema, JSON, and destination validation failures are reported
through Python logging and stop the build without a traceback.

Public models, including all standard OPK models, download without a Hugging
Face account or token. For your own private or gated model, export a read-only
`HF_TOKEN` with access to that model in the host shell before building:

```bash
export HF_TOKEN="hf_your_token_here"
./scripts/quick_start.sh
```

For VS Code, make the token available in the host environment used to start the
Dev Container, on the Pi when using Remote SSH. After changing the token, run
**Dev Containers: Rebuild Container**. Docker passes it only to the model-download
build step, not the runtime container. Direct Compose and Topo builds use the
same secret and require a fresh `HF_DOWNLOAD_CACHEBUST` from
`scripts/private/generate-hf-download-cachebust.sh` before each build.

Important input metadata includes:
- shape
- data kind, such as `ImageRgbChw` or `ImageRgbHwc`
- value type
- normalization, if required by the model

The easiest workflow is to copy one of the existing model folders and then adjust only the fields that differ.

## Validate v1 descriptors

The supported descriptor structure is defined by:

- `config/schemas/v1/model.schema.json`
- `config/schemas/v1/opchain.schema.json`

A minimal dynamic-output Model descriptor is:

```json
{
	"version": "1.0.0",
	"name": "example-onnx",
	"modelFile": "model.onnx",
	"dynamicOutput": true,
	"inputTensors": [
		{
			"shape": [1, 3, 224, 224],
			"dataKind": "ImageRgbChw"
		}
	]
}
```

From the repository root in the development container, validate every checked-in
pipeline preset, Model descriptor, and OpChain with:

```bash
opk-ci --config-schema-check
```

These JSON contracts use `MAJOR.MINOR.PATCH` string versions, initially
`"version": "1.0.0"`. Increment an existing file's patch whenever editing it
within the same major/minor contract. Major mismatches fail, minor mismatches
warn and continue, and patch differences are ignored at runtime. See
[Configuration Compatibility](../concepts/configuration-compatibility.md) for
the compatibility promise, framework and SDK ownership, and migration rules.

The validator rejects malformed JSON, duplicate keys, unsupported descriptor versions, schema
violations, and artifact-free semantic errors such as invalid tensor feedback references or
incompatible static feedback tensors. Backend-specific descriptors may share a canonical model
name. The validator does not access model artifacts or validate backend compatibility, runtime
tensor metadata, or parser output; those checks still happen when the model is loaded and run.

The schemas own local field, type, range, conditional, and built-in Op/parser attribute rules.
Attributes of custom Ops stay open because their contract belongs to that Op. The C++ semantic
pass is limited to relationships a schema cannot express directly, including cross-index tensor
checks, ordered stages and loops, and comparisons between sibling values.

## Creating the micropipeline

The micropipeline is the `opchain.json` consumed by `opkinfer`.

An OpChain can provide optional display metadata for model selectors:

- `displayName` is the user-facing model or model-chain name.
- `task` describes what the model does.
- `runtime` identifies the exact inference runtime or accelerator variant.

The loaded OpChain is the source of truth for these values. Use the exact runtime name when model
artifacts are not interchangeable. If this metadata is omitted, the browser falls back to the
internal `name` without guessing missing details.

This is the main runtime interface you should use by default when onboarding a model. In the normal path, you do not start by changing `opkinfer` or adding a new Op. You start by describing the chain with `opchain.json` and by selecting the parser that turns model outputs into structured runtime results.

A minimal model opchain typically looks like this:

```json
{
	"version": "1.0.0",
	"name": "YourModel",
	"description": "Run the example model on each video frame.",
	"displayName": "Your model",
	"task": "Object detection",
	"runtime": "ONNX",
	"ops": [
		{
			"id": "opk-std-ops/InferenceController",
			"attributes": {}
		},
		{
			"id": "opk-std-ops/GenericImagePreprocess",
			"attributes": {
				"inputImageTensorIndex": 0,
				"inputImageSourceName": "pipelineVideoFrame"
			}
		},
		{
			"id": "opk-onnx-ops/Inference",
			"attributes": {
				"modelDescriptor": "model.json"
			}
		},
		{
			"id": "opk-std-ops/GenericPostprocess",
			"attributes": {
				"parser": "<YourParser>"
			}
		}
	]
}
```

`modelDescriptor` is a filesystem path resolved from the directory containing the OpChain
descriptor, or an absolute path. Prefer `model.json` for an OpChain stored beside its model. A
reusable OpChain under `config/opchains/<name>/` can use a path such as
`../../models/<your-model>/model.json`. Path components are preserved for normal filesystem
resolution, including symlinks followed by `..`. URI values are not supported.

If your model runs on the full frame, a structure like this is usually enough.

If your model runs on crops produced by another stage, reuse an existing multi-stage example instead of inventing a new structure from scratch.

What matters here is not only that the model runs, but that the last stage produces results in the format the rest of Open Perception Kit already understands. The normal app-consumable result format is the FrameResults, carried downstream as `FrameResultsMeta`, so the parser choice is part of the model integration contract, not an optional extra.

## Reuse an existing postprocessor if possible

The built-in postprocessors currently registered in `GenericPostprocessOp` are:

- `YoloParser`
- `UltrafaceParser`
- `GazeDetectionParser`
- `CameraContactParser`
- `PaddleOcrDetectionParser`
- `ImageNetClassificationParser`
- `PersonClassificationParser`
- `ModNetSegmentationParser`
- `RvmParser`
- `ObjectEmbeddingParser`
- `DummyParser`

If one of these already matches your output format, reuse it.

If none of them matches, then your model is not plug-and-play in the current system and you will usually need to create your own postprocessor in the source tree.

## What counts as a good low-friction model here

The easiest models to integrate without code changes are models that fit one of these result types:

- `BoxDetectionsT` for detections such as faces and generic objects
- `ClassificationsT` for top-k or binary classification
- `PoseEstimationsT` for gaze estimation
- `SegmentationMasksT` for segmentation or mask outputs
- `ObjectEmbeddingsT` for ReID / embedding outputs

These are the structured result shapes that downstream Open Perception Kit code already consumes. In other words, when bringing a model into Open Perception Kit, you are usually trying to map raw tensors into one of these generated FrameResults payloads rather than inventing a model-specific application contract.

If your output shape and meaning already match one of the existing parsers, integration is usually straightforward.

If they do not, the model can still be integrated, but you should expect to add a postprocessor.

## Validation checklist

Before considering the integration complete, verify that:

- `opk-ci --config-schema-check` accepts the descriptor structure
- the runtime can load the model file
- `model.json` matches the real input and output expectations
- preprocessing matches layout, value type, and normalization requirements
- the chosen parser can parse the outputs without guessing
- the result appears correctly in the running pipeline
- the model works in a real pipeline, not only in isolation

## Normal workflow

The normal workflow is:

1. create `config/models/<your-model>/` and its descriptors
2. for local development, place the artifact at `modelFile` in the bind-mounted
   checkout; for a container image, add pinned `hfDownload` arguments unless
   the repository explicitly allowlists the local binary
3. prefer a descriptor-relative `modelFile`; use an absolute path only when the
   deployment owns that stable location
4. create or update an `opchain.json`
5. optionally add a top-level pipeline preset under `config/pipelines/`
6. run `opk-ci --config-schema-check` inside the container
7. rebuild the container when the model is published; export a valid
   `HF_TOKEN` only when the artifact is private or gated
8. confirm the built image contains every artifact referenced by that pipeline
9. run the pipeline with the VS Code run task "00 Run project and select pipeline" or `tools/opk-menu`
10. update the model and opchain `index.md` files

## What you should try not to change first

For a normal bring-your-own-model task, try to stay within:
- `config/models/`
- `config/opchains/`
- `config/pipelines/`

If that is not enough, the next most common place to change is:

- `development/ops-std/postproc/` for a new postprocessor

Those are the intended user-facing extension points for the common path. If you stay within model descriptors, opchains, pipeline presets, and parser selection, you are still using the default integration surface.

If you need to go beyond that and change elements or core runtime behavior, the task has moved beyond a simple model drop-in.

## Good examples to copy from

- `config/models/yolo26n-320/` for a simple object detector
- `config/models/ultraface-rfb-320/` for a simple face detector
- `config/opchains/mobilegaze-mobilenet-v2/` for a detector + gaze cascade
- `config/opchains/nitec-resnet-18/` for a detector + classification cascade

## If the built-in parsers are not enough

If your model output does not match any built-in parser, the next step is custom postprocessing.

That topic is covered separately in [Custom postprocessing](custom-postprocessing.md).

## What should you have at the end of this document?

By the end of this page, you should have:

- a new or adapted model folder under `config/models/`
- descriptor metadata that matches the real model contract
- an `opchain.json` that points to the right inference backend and parser
- a realistic answer to whether the model is low-friction in the current runtime

Success looks like this: OPK can load the model, the pipeline runs, the selected parser matches the outputs, and the result appears correctly in the runtime.

[Back to How-To Guides](/how-to)
