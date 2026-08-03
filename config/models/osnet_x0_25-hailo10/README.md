# OSNet x0.25 Hailo 10

Hailo 10-compiled variant of the OSNet x0.25 embedding model.

- Backend: HailoRT
- Input: NCHW crop, `[1, 3, 256, 128]`, normalized with ImageNet mean/std
- Output: dynamic embedding tensor, typically matching the ONNX variant
- Post processor: `ObjectEmbeddingParser`
- Supported FrameResults payload: `ObjectEmbeddingsT` with `content_type` set to `objectEmbedding`
- Note: this `.hef` is the compiled Hailo version of the original ONNX model
- Typical pairing: `config/pipelines/04-full-onnx-hailo10.json`

# Export Tutorial

This guide shows the minimal flow to export the OSNet x0.25 model for Hailo.

**Disclaimer:** Run this workflow on a PC capable of compiling the model. The target device may not be suitable.

## 1) Patch the ONNX model

Some pipelines require explicit `kernel_shape` on Conv nodes.
Run the patch script first to generate the patched ONNX model.

```bash
python patch_kernel_shape.py
```

Expected output artifact:
- `osnet_x0_25_msmt17_opset21_patched.onnx`

## 2) Parse ONNX to HAR

Convert the original ONNX network into Hailo HAR format with the expected input tensor shape.

```bash
hailo parser onnx osnet_x0_25_msmt17_opset21_patched.onnx --tensor-shapes [1,3,256,128]
```

## 3) Optimize the patched HAR

Apply optimization using random calibration data and the provided model script.

```bash
hailo optimize osnet_x0_25_msmt17_opset21_patched.har --use-random-calib-set --model-script fix_avgpool.alls
```

Expected output artifact:
- `osnet_x0_25_msmt17_opset21_patched_optimized.har`

## 4) Compile to deployable output

Compile the optimized HAR into the final target artifact.

```bash
hailo compiler osnet_x0_25_msmt17_opset21_patched_optimized.har
```

This command generates the compiled model output used for deployment.
