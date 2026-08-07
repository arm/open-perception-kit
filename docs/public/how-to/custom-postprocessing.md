---
title: Custom Postprocessing
sidebar_position: 5
sidebar_label: Custom Postprocessing
description: Add a parser when a model's output tensors do not fit an existing Perception XPK postprocessor.
---

# Custom Postprocessing

This page covers the next step after the normal model-integration path: writing or generating a parser when the built-in postprocessors are not enough.

## What will you learn from this documentation?

If you follow this page successfully, you will learn when custom postprocessing is the right extension point and how to turn a model-specific output tensor contract into a parser that produces meaningful `Perception` results.

At the end of this page, you should know what custom code belongs in a parser, how to register it, how to reference it from an `opchain.json`, and how to judge whether the result is ready for visualization.

Only continue with this page after you have already established that:

- the normal `opchain.json` structure is correct for your model
- an existing parser will not fit your outputs cleanly
- the built-in preprocessing path is already sufficient for your input preparation

In practice, preprocessing is often the easy part. Resizing, color conversion, normalization, and writing an input image tensor are usually covered by the normal flow.

In this codebase, the part that most often differs from model to model is postprocessing, because it depends on the exact output tensor shape and on what the tensor values mean.

At that point, the place you usually need to extend is `development/ops-std/postproc/`, together with the parser selection inside `GenericPostprocessOp`.

That is also why custom postprocessing is the default extension path before any deeper runtime work. If the model already runs and only the meaning of the outputs is missing, the intended interface is the parser layer plus the matching `opchain.json` reference.

## Why custom postprocessing is usually the right extension point

That is the right level for most model-specific work because:

- preprocessing and inference are already done
- `GenericPostprocessOp` already collects the output tensors into a parser input
- the parser already receives `inferenceInfo`, and `GenericPostprocessOp` links parsed results back to the current inference source
- you only need to translate model outputs into `Perception` objects

Those `Perception` objects are the structured results that the rest of PEK consumes downstream. In the normal flow, the parser is the step that turns raw tensor output into the app-usable runtime format.

The best example to follow is the camera-contact flow used by the `cam-connect` pipeline:

1. a first model detects faces on the full frame
2. `InferenceController` collects those face rectangles into `inferenceImageCrops`
3. the standard preprocessing step prepares each detected face crop for inference
4. the classifier produces a `[1,2]` output tensor for contact vs no-contact
5. `CameraContactParser` interprets those logits and writes the result into `Perception`

This is exactly the kind of problem a custom postprocessor should solve.

## What the built-in postprocessor already does

`GenericPostprocessOp` in `development/ops-std/` is the dispatcher that selects a tensor parser by name.

Its current contract is:

- it reads the configured `parser` attribute
- it constructs the matching parser implementation, such as `YoloParser` or `CameraContactParser`
- during `process()` it passes the active output tensors and `inferenceInfo` into that parser
- it expects the parser to fill a `Perception::Layer` with meaningful detections or classifications
- it attaches the parsed results back to the current perception result

If your model output does not match any of the built-in parsers, this is the point where you add a new one.

## Easiest implementation path

The shortest practical path is:

1. copy a parser that is close to your output format, such as `CameraContactParser.cpp`, `ImageNetClassificationParser.cpp`, or `YoloParser.cpp`
2. rename it to something model-specific
3. keep the same `parse()` structure
4. change the tensor shape checks and tensor decoding logic to match your model
5. add the new parser source file under `development/ops-std/postproc/`
6. add that source file to `development/ops-std/meson.build`
7. register the parser name in `GenericPostprocessOp.cpp`
8. add its closed attribute schema under
   `config/schemas/v1/opchain/ops/generic-postprocess/`
9. add that schema's `$ref` to `generic-postprocess.schema.json`
10. add its `$id` and path to the schema manifest in `development/config-validator/meson.build`
11. reference that parser name from the relevant `opchain.json`
12. run `expkits-ci --config-schema-check` in the development container

This keeps the change local to the inference chain and avoids touching `pekinfer` or the outer GStreamer pipeline.

That local change is usually a good sign that you are still within the intended extension surface.
When the work can stay inside parser code, build wiring, and `opchain.json`, you usually do not need a new Op.
For normal model onboarding, adding a parser is the intended path; changing inference Ops or deeper runtime code is outside the common user extension surface.

## Alternative path: use a well-specified integration prompt

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

## How to think about the cam-connect example

For camera contact, the model-specific job is not “prepare one image”. The model-specific job is “interpret the classifier output correctly”.

That means your parser should usually:

- verify that the output tensor shape is what the model actually emits
- read the tensor values in the correct order
- apply any confidence logic, thresholding, class index mapping, or decoding rules required by that model
- create the right `Perception` object type
- produce results that can be linked back to the source face or source detection

In other words, preprocessing prepares pixels, but postprocessing explains meaning. That meaning is what usually changes from one model to another.

## What usually belongs in the custom code

In this codebase, “custom code” usually means a small and specific set of files, not a broad runtime rewrite.

If you can reuse an existing `Perception` structure such as `Rect`, `Classification`, `YawPitch`, `SegmentationMap`, or `ObjectEmbedding`, the usual files to touch are:

1. create a new parser header under `development/ops-std/postproc/<YourParser>.h`
2. create a new parser implementation under `development/ops-std/postproc/<YourParser>.cpp`
3. add that `.cpp` file to `development/ops-std/meson.build`
4. include and register the parser in `development/ops-std/GenericPostprocessOp.cpp`
5. add the parser's closed schema resource, parent dispatcher `$ref`, and schema manifest entry
6. reference the parser name from the model's `opchain.json`
7. run the descriptor gate

That is the normal path when the output tensor meaning is new, but the result still fits an existing `Perception` type.

If you need a genuinely new `Perception` structure because none of the existing detection types matches your result cleanly, the usual files to touch are:

1. `development/common/pek/Perception.h`
	- add the new struct
	- add it to `Perception::Detection`
2. `development/common/pek/PerceptionSerializer.h`
	- declare `to_json()` for the new struct if it needs to be serialized out of process
3. `development/common/pek/PerceptionSerializer.cpp`
	- implement `to_json()` for the new struct
	- add it to the `Perception::Detection` variant serializer
4. `development/ops-std/postproc/<YourParser>.h`
	- declare the parser that produces the new structure
5. `development/ops-std/postproc/<YourParser>.cpp`
	- create and fill the new `Perception` object
	- set `layer.contentType` to the content type you want downstream code to look for
6. `development/ops-std/meson.build`
	- compile the new parser source file
7. `development/ops-std/GenericPostprocessOp.cpp`
	- include the parser header
	- instantiate it from the `parser` attribute string
8. the parser's closed schema resource, parent dispatcher `$ref`, and schema manifest entry
9. the relevant `config/models/<model>/opchain.json` or `config/opchains/.../opchain.json`
	- route inference output into that parser by name
10. `expkits-ci --config-schema-check`
	- verify the new contract and every checked-in descriptor

If another downstream element needs to understand the new `contentType`, you may also need to update that element. The common example is `development/elements/pekosd/pekosd.cpp` for overlay rendering.

So the routing path is usually:

- parser implementation produces a `Perception::Layer`
- `layer.contentType` names the semantic result category
- `GenericPostprocessOp` pushes that layer into `Perception`
- downstream elements such as `pekosd` or `pektracker` look for that `contentType`

If your model output already matches one of the built-in parsers, prefer reusing that parser instead of creating a new one.

## Visualizing the result in the current runtime

Once your parser writes the right `Perception` results, those results can be visualized by `pekosd` when server-side overlays are enabled.

`pekosd` is the element that currently does server-side drawing. It reads `PerceptionMeta` from the video buffer and renders supported result layers onto the BGRA frame.

That means the usual flow is:

1. your parser converts raw tensors into a `Perception::Layer`
2. each detection in that layer gets linked back to the current inference source through `parentUuid`
3. `GenericPostprocessOp` appends the layer to `Perception`
4. `PerceptionMeta` carries that structured data downstream with the buffer
5. when enabled, `pekosd` reads the resulting layers and decides what to draw based on `layer.contentType` and the detection variant type

This is how the checked-in camera-contact flow works as well: the parser produces a `cameraContact` result, and `pekosd` can render that as a green or red status dot when enabled.

So when bringing your own model, you should think about two separate questions:

- how do I convert the output tensor into the right `Perception` structure?
- does `pekosd` already know how to draw that structure?

If the answer to the second question is yes, then you only need the parser.

If the answer is no, then the parser may still be correct, but you will also need to extend `pekosd` so the new result type has a visible overlay.

In practice, “make the data make sense” means:

- pick the right `Perception` structure for the meaning of the output
- fill its fields in normalized image coordinates or the expected runtime units
- make sure the OpChain is feeding the correct source object so `GenericPostprocessOp` can set `parentUuid` correctly
- choose a stable `layer.contentType` string that downstream code can match on

Then, for visualization, choose the overlay style that matches the semantics of the data:

- boxes or circles for detections tied to image regions
- text lists for classifications
- arrows or vectors for directional values such as gaze
- mask overlays for segmentation
- custom symbols only when the existing styles do not fit the meaning well

For a new visualization path, the file to extend is usually `development/elements/pekosd/pekosd.cpp`.

The usual pattern there is:

1. check `layer.contentType`
2. read the expected `Perception` variant from `layer.detections`
3. find the parent region if the drawing depends on an earlier detection
4. draw the overlay with the existing `Osd::*` helpers

So the practical rule is:

- if the parser output already matches an existing `pekosd` branch, reuse that path
- if the parser output is structurally new, add a new drawing branch in `pekosd.cpp`
- if the result is meaningful for machines but not useful as an overlay, it is acceptable to keep it in `Perception` without drawing it immediately

## Minimal opchain shape for this pattern

The usual chain shape is still:

```json
{
	"version": 1,
	"name": "CameraContact",
	"description": "Estimate camera contact for each detected face.",
	"ops": [
		{
			"id": "pek-std-ops/InferenceController",
			"loopId": 1,
			"attributes": {
				"contentType": "humanFace"
			}
		},
		{
			"id": "pek-std-ops/GenericImagePreprocess",
			"loopId": 1,
			"attributes": {
				"inputImageTensorIndex": 0,
				"inputImageSourceName": "pipelineVideoFrame"
			}
		},
		{
			"id": "pek-onnx-ops/Inference",
			"loopId": 1,
			"attributes": {
				"modelDescriptor": "model.json"
			}
		},
		{
			"id": "pek-std-ops/GenericPostprocess",
			"loopId": 1,
			"attributes": {
				"parser": "CameraContactParser",
				"contactClassIndex": 1,
				"noContactClassIndex": 0
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
- This is an absolutely minimal opchain and it still requires a different operation to create the humanFace content.

If you stay within that structure, a custom postprocessor is usually a small and contained change.

## What should you have at the end of this document?

By the end of this page, you should have:

- a clear reason why the built-in parsers are not sufficient
- a concrete parser implementation or a precise parser-generation prompt
- the parser registered in `GenericPostprocessOp`
- the parser's schema resource registered in the parent dispatcher and schema bundle
- an `opchain.json` that references the new parser name
- a successful `expkits-ci --config-schema-check`

Success looks like this: your model outputs are translated into the right `Perception` structure, and the runtime can consume those results without guessing.

[Back to How-To Guides](/how-to)
