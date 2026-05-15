# Use Your Own Media

Use this page after a quick start works with the included sample media.

Start from a known-working pipeline, change only the source section, and run
the pipeline again. Leave the inference, overlay, performance, and sink stages
unchanged until the new input is working.

Most video-oriented PEK pipelines expect `BGRA` frames before inference and
overlay stages, so keep the conversion to `video/x-raw,format=BGRA` unless you
are intentionally changing that runtime contract.

## Use an image file

Put your image under `data/images/`, then edit a pipeline under
`config/pipelines/` to use it as the source.

For a JPEG image, use this pattern:

```text
filesrc location=/work/data/images/my-image.jpg !
jpegdec !
imagefreeze !
videoconvert ! video/x-raw,format=BGRA !
```

Run the same pipeline again after saving the file.

## Use a video file

Put your video under `data/videos/`, then edit a pipeline under
`config/pipelines/` to use it as the source.

For a local video file, use this pattern:

```text
filesrc location=/work/data/videos/my-video.mp4 !
decodebin name=dec
dec. ! queue ! videoconvert ! videoscale ! video/x-raw,format=BGRA !
```

Run the same pipeline again after saving the file.

## Use a media stream

Use the GStreamer source element that matches your stream, then keep the
downstream conversion into `BGRA`.

For example, a network or platform stream usually replaces only the source and
decode part of the pipeline. Keep the rest of the known-working pipeline in
place:

```text
<stream-source-and-decode> !
videoconvert ! videoscale ! video/x-raw,format=BGRA !
```

Checked-in pipeline presets under `config/pipelines/` include alternative
source examples. Use those as templates before changing inference or sink
behavior.

## Use a camera

For live camera input, use [Use A Camera](camera-input.md). That page covers
USB cameras, Raspberry Pi camera input, and the checks to run before changing a
pipeline.

For ready-to-run live camera presets, use:

- `05-full-onnx-raspicam` for a Raspberry Pi camera.
- `06-full-onnx-usb-cam` for a USB camera at `/dev/video0`.

[Back to README](../README.md)
