---
sidebar_position: 32
sidebar_label: Bring your model
---

# Bring Your Model

This page describes the shortest practical path for bringing your own model into AMP with as little runtime-code change as possible.

## Supported model formats

The codebase currently supports these runtime/model combinations:

- ONNX Runtime with `.onnx` models
- HailoRT with `.hef` models
- ExecuTorch with `.pte` models, currently experimental

If you want the least friction, start with ONNX and reuse an existing output parser.

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

## Creating the micro-pipeline

The micro-pipeline is the `opchain.json` consumed by `ampinfer`.

A minimal model opchain typically looks like this:

```json
{
	"ops": [
		{
			"id": "amp-std-ops/InferenceController",
			"attributes": {}
		},
		{
			"id": "amp-std-ops/GenericImagePreprocess",
			"attributes": {
				"inputImageTensorIndex": 0,
				"inputImageSourceName": "pipelineVideoFrame"
			}
		},
		{
			"id": "amp-onnx-ops/Inference",
			"attributes": {
				"modelDescriptor": "/work/config/models/<your-model>/model.json"
			}
		},
		{
			"id": "amp-std-ops/GenericPostprocess",
			"attributes": {
				"parser": "<YourParser>"
			}
		}
	]
}
```

If your model runs on the full frame, a structure like this is usually enough.

If your model runs on crops produced by another stage, reuse an existing multi-stage example instead of inventing a new structure from scratch.

## Reuse an existing postprocessor if possible

The built-in post processors currently registered in `GenericPostprocessOp` are:

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
5. run the pipeline with `tools/amp-menu`
6. update the model and opchain `README.md` files

## What you should try not to change first

For a normal bring-your-own-model task, try to stay within:
- `config/models/`
- `config/opchains/`
- `config/pipelines/`

If that is not enough, the next most common place to change is:

- `development/ops-std/postproc/` for a new postprocessor

If you need to go beyond that and change elements or core runtime behavior, the task has moved beyond a simple model drop-in.
## Good examples to copy from

- `config/models/yolov11/` for a simple object detector
- `config/opchains/tracking/` for a detector + embedding cascade
- `config/models/mobilenetv2/` for a simple classifier
- `config/models/modnet/` for segmentation
- `config/models/osnet_x0_25/` for embeddings

## When the defaults are not enough: custom postprocessing

Only continue with this section after you have already established that:

- the normal `opchain.json` structure is correct for your model
- an existing parser will not fit your outputs cleanly
- the built-in preprocessing path is already sufficient for your input preparation

In practice, preprocessing is often the easy part. Resizing, color conversion, normalization, and writing an input image tensor are usually covered by the normal flow.

In this codebase, the part that most often differs from model to model is postprocessing, because it depends on the exact output tensor shape and on what the tensor values mean.

At that point, the place you usually need to extend is `development/ops-std/postproc/`, together with the parser selection inside `GenericPostprocessOp`.

That is the right level for most model-specific work because:

- preprocessing and inference are already done
- `GenericPostprocessOp` already collects the output tensors into a parser input
- the parser already receives `inferenceInfo`, and `GenericPostprocessOp` links parsed results back to the current inference source
- you only need to translate model outputs into `Perception` objects

The best example to follow is the camera-contact flow used by the `cam-connect` pipeline:

1. a first model detects faces on the full frame
2. `InferenceController` collects those face rectangles into `inferenceImageCrops`
3. the standard preprocessing step prepares each detected face crop for inference
4. the classifier produces a `[1,2]` output tensor for contact vs no-contact
5. `CameraContactParser` interprets those logits and writes the result into `Perception`

This is exactly the kind of problem a custom postprocessor should solve.

### What the built-in postprocessor already does

`GenericPostprocessOp` in `development/ops-std/` is the dispatcher that selects a tensor parser by name.

Its current contract is:

- it reads the configured `parser` attribute
- it constructs the matching parser implementation, such as `YoloParser` or `CameraContactParser`
- during `process()` it passes the active output tensors and `inferenceInfo` into that parser
- it expects the parser to fill a `Perception::Layer` with meaningful detections or classifications
- it attaches the parsed results back to the current perception result

If your model output does not match any of the built-in parsers, this is the point where you add a new one.

### Easiest implementation path

The shortest practical path is:

1. copy a parser that is close to your output format, such as `CameraContactParser.cpp`, `ImageNetClassificationParser.cpp`, or `YoloParser.cpp`
2. rename it to something model-specific
3. keep the same `parse()` structure
4. change the tensor shape checks and tensor decoding logic to match your model
5. add the new parser source file under `development/ops-std/postproc/`
6. add that source file to `development/ops-std/meson.build`
7. register the parser name in `GenericPostprocessOp.cpp`
8. reference that parser name from the relevant `opchain.json`

This keeps the change local to the inference chain and avoids touching `ampinfer` or the outer GStreamer pipeline.

### Alternative path: use a well-specified integration prompt

You do not always have to hand-write the parser first.

An alternative is to give an agent a prompt similar to the integration prompt used for camera contact and ask it to generate the needed postprocessing changes.

This can work well, but only if the prompt is filled in with technically correct model details. The most important inputs are:

- the exact output tensor shapes
- the output tensor value type
- which tensor index contains which data
- what each tensor value means
- class ordering, thresholds, anchors, or decoding rules if they exist
- the target `Perception` result type you want to produce

If those details are vague or wrong, the generated postprocessor will also be wrong.

For example, the camera-contact prompt works because it clearly states that the model output is `FLOAT[1,2]` and that those two values represent the `no contact` and `contact` classes. That is the kind of information an agent needs in order to generate a correct parser.

In short: if you want an agent to create the postprocessor for you, give it the exact tensor contract, not just the model name.

### How to think about the cam-connect example

For camera contact, the model-specific job is not “prepare one image”. The model-specific job is “interpret the classifier output correctly”.

That means your parser should usually:

- verify that the output tensor shape is what the model actually emits
- read the tensor values in the correct order
- apply any confidence logic, thresholding, class index mapping, or decoding rules required by that model
- create the right `Perception` object type
- produce results that can be linked back to the source face or source detection

In other words, preprocessing prepares pixels, but postprocessing explains meaning. That meaning is what usually changes from one model to another.

### What usually belongs in the custom code

Only add custom code for behavior that the existing parsers cannot express cleanly, for example:

- model-specific tensor layouts
- custom logits-to-class rules
- anchor decoding or box decoding logic
- output tensors spread across multiple buffers
- nonstandard segmentation or embedding output formats

If your model output already matches one of the built-in parsers, prefer reusing that parser instead of creating a new one.

### Visualizing the result in the current runtime

Once your parser writes the right `Perception` results, those results can already be visualized by `amposd`.

`amposd` is the element that currently does the drawing. It reads `PerceptionMeta` from the video buffer and renders supported result layers onto the BGRA frame.

That means the usual flow is:

1. your parser converts raw tensors into `Perception`
2. `amposd` reads those `Perception` layers downstream
3. `amposd` draws the overlay on the video frame

This is how the checked-in camera-contact flow works as well: the parser produces a `cameraContact` result, and `amposd` renders that as a green or red status dot.

So when bringing your own model, you should think about two separate questions:

- how do I convert the output tensor into the right `Perception` structure?
- does `amposd` already know how to draw that structure?

If the answer to the second question is yes, then you only need the parser.

If the answer is no, then the parser may still be correct, but you will also need to extend `amposd` so the new result type has a visible overlay.

### Minimal opchain shape for this pattern

The usual chain shape is still:

```json
{
	"ops": [
		{
			"id": "amp-std-ops/InferenceController",
			"attributes": {
				"contentType": "humanFace"
			}
		},
		{
			"id": "amp-std-ops/GenericImagePreprocess",
			"attributes": {}
		},
		{
			"id": "amp-onnx-ops/Inference",
			"attributes": {
				"modelDescriptor": "/work/config/models/cam-contact/model.json"
			}
		},
		{
			"id": "amp-std-ops/GenericPostprocess",
			"attributes": {
				"parser": "<YourParser>"
			}
		}
	]
}
```

The important part is the division of responsibility:

- `InferenceController` chooses the image regions
- `GenericImagePreprocess` converts those regions into model input tensors
- the inference Op runs the model
- the parser inside `GenericPostprocess` turns outputs into `Perception` results

If you stay within that structure, a custom postprocessor is usually a small and contained change.
