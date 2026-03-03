# Example OpChain in ampinfer
## Model Cascading: Face-Driven Crop Loop With Preprocess → Inference → Postprocess

This example shows a typical OpChain executed inside the `ampinfer` GStreamer element.
The chain runs inference over multiple regions of interest derived from Perception content.
The main pattern is: collect ROIs → create crops → loop over a subchain while crops remain.

<img src="resources/img/opchain-example.png" alt="Example OpChain" width="400">

---

## Chain Layout

The chain is composed of four Ops executed in order.
All Ops are assigned to the same group so they can participate in the same inference loop.
Each Op can read transient execution data from OpChainContext and write persistent results into Perception.

- `amp-std-ops/InferenceController`
- `amp-std-ops/GenericImagePreprocess`
- `amp-onnx-ops/Inference`
- `amp-std-ops/GenericPostprocess`

---

## InferenceController Loop Semantics

`InferenceController` drives the multi-crop execution flow.
It selects input regions based on `contentType`.
It queries Perception for all rectangles matching the content type (e.g. detected faces).
It converts these rectangles into logical crops for inference.
It pushes the resulting crop list into `OpChainContext::inferenceCrops`.
It then repeatedly executes the Ops in `inferenceLoopGroup` while consuming crops.
One crop is removed per iteration.
Looping stops when the crop list becomes empty.

This allows a single frame to produce multiple inference executions.
This design supports “for each object” inference, such as “for each detected face” gaze estimation.

---

## Data Flow Summary

A video frame enters the pipeline and is exposed as `bitmapViews["pipelineVideoFrame"]`.
InferenceController creates crop rectangles based on Perception content.
GenericImagePreprocess builds the input tensor for the current crop.
Inference runs the model for the current crop using the selected runtime backend.
GenericPostprocess parses output tensors and writes structured metadata into Perception.

---

## JSON Descriptor Example

The OpChain is defined declaratively via JSON.
The `group` field ties Ops into a named execution group.
`InferenceController` uses `inferenceLoopGroup` to select which group is iterated.

```json
{
	"ops": [

	{
		"id": "amp-std-ops/InferenceController",
		"attributes": {
		}
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
			"modelDescriptor": "/work/etc/models/ultraface/ultraface.json"
        }
	},
    {
		"id": "amp-std-ops/GenericPostprocess",
		"attributes": {
			"parser": "UltrafaceParser",
			"normalizeOutputCoordinates": false,
			"confidenceThreshold": 0.3,
			"iouThreshold": 0.1
		}
	},
	{
		"id": "amp-std-ops/InferenceController",
		"group": "for-all-faces",
		"attributes": {
			"contentType": "humanFace",
		}
	},
	{
		"id": "amp-std-ops/GenericImagePreprocess",
		"group": "for-all-faces",
		"attributes": {
			"inputImageTensorIndex": 0
		}
	},
	{
		"id": "amp-onnx-ops/Inference",
		"group": "for-all-faces",
        "attributes": {
			"modelDescriptor": "/work/etc/models/gazedetection/model.json"
        }
	},
    {
		"id": "amp-std-ops/GenericPostprocess",
		"group": "for-all-faces",
		"attributes": {
			"parser": "GazeDetectionParser",
			"normalizeOutputCoordinates": false,
			"confidenceThreshold": 0.5,
			"iouThreshold": 0.3
		}
	}
  ]
}
```

## Notes

The example uses ONNX inference via amp-onnx-ops/Inference.
The same pattern applies to other runtimes by swapping the inference Op implementation.
Perception is the persistent container that travels downstream and accumulates results across Ops and across GStreamer ampinfer element intances.
OpChainContext is transient and only valid during execution of the current chain.
