# OSNet x0_25 ONNX (ReID Embeddings)

## Source
- Model: `osnet_x0_25_msmt17.onnx`
- Exported from BoxMOT pretrained weights: `osnet_x0_25_msmt17.pt`
- Backbone: OSNet x0_25 (appearance ReID embedding network)

## Integration parameters
- Input tensor name: `images`
- Input shape: `[1, 3, 256, 128]` (NCHW)
- Input dtype: `float32`
- Color/order: RGB
- Preprocess: scale to `[0,1]`, then normalize with mean `[0.485, 0.456, 0.406]` and std `[0.229, 0.224, 0.225]`

- Output tensor name: `output0`
- Output shape: `[1, 512]`
- Postprocess: L2-normalize embedding, then use cosine distance/similarity for association
