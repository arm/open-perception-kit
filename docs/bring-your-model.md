# Bring Your Model

This page describes the shortest practical path for bringing your own model into PEK with as little runtime-code change as possible.

## What will you learn from this documentation?

If you follow this page successfully, you will learn how to add a model to PEK by reusing the existing descriptor, opchain, and parser structure wherever possible.

At the end of this page, you should have a model folder, a matching `model.json`, a working `opchain.json`, and a clear decision on whether an existing parser is enough or whether you need custom postprocessing.

## Supported model formats

The codebase currently supports these runtime/model combinations:

- ONNX Runtime with `.onnx` models
- HailoRT with `.hef` models

If you want the least friction, start with ONNX and reuse an existing output parser.

## Checked-in Hailo naming pattern

The repository now keeps compiled Hailo variants in accelerator-specific model folders rather than in one generic `*-hef` bucket.

Current checked-in examples include:

- `config/models/mobilenetv2-hailo8/`
- `config/models/mobilenetv2-hailo8l/`
- `config/models/mobilenetv2-hailo10/`
- `config/models/osnet_x0_25-hailo8/`
- `config/models/osnet_x0_25-hailo8l/`
- `config/models/osnet_x0_25-hailo10/`
- `config/models/yolov11-hailo8/`
- `config/models/yolov11-hailo8l/`

The matching full-demo presets are:

- `config/pipelines/02-full-onnx-hailo8.json`
- `config/pipelines/03-full-onnx-hailo8l.json`
- `config/pipelines/04-full-onnx-hailo10.json`

If you are adding another compiled Hailo model, follow that same naming pattern so the pipeline can select the intended accelerator generation explicitly.
Hailo 8 and Hailo 8L compiled model files are not interchangeable, so keep those variants in separate folders and use the matching pipeline preset.

## Minimum files for a new model

The usual place for a new model is:

```text
config/models/<your-model>/
```

At minimum, that folder should contain:
- the model file
- `model.json`
- usually `opchain.json`
- `README.md`

For most users, these files are the main integration interface of the system. The default path is to describe the model with `model.json`, connect it with `opchain.json`, and let the existing runtime elements do the rest.

## Required descriptor metadata

`model.json` is the runtime descriptor used by the inference Op.

Typical fields are:
- `name`
- `modelFamily`
- `modelFile`
- `dynamicOutput`
- `contentType` when applicable
- `inputTensors`
- `outputTensors` when outputs are static

Important input metadata includes:
- shape
- data kind, such as `ImageRgbChw` or `ImageRgbHwc`
- value type
- normalization, if required by the model

The easiest workflow is to copy one of the existing model folders and then adjust only the fields that differ.

## Creating the micropipeline

The micropipeline is the `opchain.json` consumed by `pekinfer`.

This is the main runtime interface you should use by default when onboarding a model. In the normal path, you do not start by changing `pekinfer` or adding a new Op. You start by describing the chain with `opchain.json` and by selecting the parser that turns model outputs into structured runtime results.

A minimal model opchain typically looks like this:

```json
{
	"ops": [
		{
			"id": "pek-std-ops/InferenceController",
			"attributes": {}
		},
		{
			"id": "pek-std-ops/GenericImagePreprocess",
			"attributes": {
				"inputImageTensorIndex": 0,
				"inputImageSourceName": "pipelineVideoFrame"
			}
		},
		{
			"id": "pek-onnx-ops/Inference",
			"attributes": {
				"modelDescriptor": "/work/config/models/<your-model>/model.json"
			}
		},
		{
			"id": "pek-std-ops/GenericPostprocess",
			"attributes": {
				"parser": "<YourParser>"
			}
		}
	]
}
```

If your model runs on the full frame, a structure like this is usually enough.

If your model runs on crops produced by another stage, reuse an existing multi-stage example instead of inventing a new structure from scratch.

What matters here is not only that the model runs, but that the last stage produces results in the format the rest of PEK already understands. The normal app-consumable result format in PEK is `Perception`, carried downstream as `PerceptionMeta`, so the parser choice is part of the model integration contract, not an optional extra.

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

- `Perception::Rect` for detections such as faces and generic objects
- `Perception::Classification` for top-k or binary classification
- `Perception::YawPitch` for gaze estimation
- `Perception::SegmentationMap` for segmentation or mask outputs
- `Perception::ObjectEmbedding` for ReID / embedding outputs

These are the structured result shapes that downstream PEK code already consumes. In other words, when bringing a model into PEK, you are usually trying to map raw tensors into one of these `Perception` forms rather than inventing a model-specific application contract.

If your output shape and meaning already match one of the existing parsers, integration is usually straightforward.

If they do not, the model can still be integrated, but you should expect to add a postprocessor.

## Validation checklist

Before considering the integration complete, verify that:

- the runtime can load the model file
- `model.json` matches the real input and output expectations
- preprocessing matches layout, value type, and normalization requirements
- the chosen parser can parse the outputs without guessing
- the result appears correctly in the running pipeline
- the model works in a real pipeline, not only in isolation

## Normal workflow

The normal workflow is:

1. place the model and descriptors in `config/models/<your-model>/`
2. create or update an `opchain.json`
3. optionally add a top-level pipeline preset under `config/pipelines/`
4. build inside the container
5. run the pipeline with the VS Code run task "00 Run project and select pipeline" or `tools/pek-menu`
6. update the model and opchain `README.md` files

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

- `config/models/yolov11/` for a simple object detector
- `config/models/yolov11-hailo8/` for a Hailo 8 detector variant
- `config/opchains/tracking/` for a detector + embedding cascade
- `config/models/mobilenetv2/` for a simple classifier
- `config/models/mobilenetv2-hailo8/` and `config/models/mobilenetv2-hailo10/` for compiled Hailo classifier variants
- `config/models/modnet/` for segmentation
- `config/models/osnet_x0_25/` for embeddings
- `config/models/osnet_x0_25-hailo8/` and `config/models/osnet_x0_25-hailo10/` for compiled Hailo embedding variants

## If the built-in parsers are not enough

If your model output does not match any built-in parser, the next step is custom postprocessing.

That topic is covered separately in [Custom postprocessing](custom-postprocessing.md).

## What should you have at the end of this document?

By the end of this page, you should have:

- a new or adapted model folder under `config/models/`
- descriptor metadata that matches the real model contract
- an `opchain.json` that points to the right inference backend and parser
- a realistic answer to whether the model is low-friction in the current runtime

Success looks like this: PEK can load the model, the pipeline runs, the selected parser matches the outputs, and the result appears correctly in the runtime.
