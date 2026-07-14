---
title: Use A Camera
sidebar_position: 2
sidebar_label: Use A Camera
description: Switch a working Perception XPK pipeline to a USB camera or Raspberry Pi camera source.
---

# Use A Camera

Use this page after a quick start works with the checked-in sample media.

The first camera test should change only the input source. Keep the rest of the pipeline unchanged so you know any failure is related to camera input, not model or sink changes.

If you want a live camera source enabled by default, use one of the checked-in
camera presets first:

- `05-full-onnx-raspicam` for a Raspberry Pi CSI camera.
- `06-full-onnx-usb-cam` for a USB camera exposed as `/dev/video0`.

Use the manual source-editing path below when your camera device, camera name,
resolution, or pipeline preset needs to differ from those defaults.

## 1. Choose A Pipeline File

For live ONNX camera runs, start with one of these files:

- `config/pipelines/05-full-onnx-raspicam.json`
- `config/pipelines/06-full-onnx-usb-cam.json`

For Hailo or custom preset work, open one of these files and replace the source
section manually:

- `config/pipelines/02-full-onnx-hailo8.json`
- `config/pipelines/03-full-onnx-hailo8l.json`
- `config/pipelines/04-full-onnx-hailo10.json`

## 2. Find The Source Section

At the top of the `pipeline` array, the source section defines the input.
In sample-media presets, it usually looks like this:

```json
"filesrc location=/work/data/videos/GettyImages-1140581459.mov !",
"decodebin !",
"videoconvert !",
"video/x-raw,format=BGRA !",
```

Replace only the source lines. Leave the `pekinfer`, `pektracker`, `pekperformance`, `pekosd`, and `peksink` lines as they are.

## 3. Use A USB Camera

On Raspberry Pi or Linux, check the USB camera path in the **host shell** or **Raspberry Pi shell**:

```bash
v4l2-ctl --list-devices
```

If your camera appears as `/dev/video0`, replace the source lines with:

```json
"v4l2src device=/dev/video0 ! \"image/jpeg,width=1280,height=720,framerate=30/1\" !",
"jpegdec !",
"videoconvert ! video/x-raw,format=BGRA !",
```

If your camera appears as a different device, replace `/dev/video0` with the correct path.

## 4. Use A Raspberry Pi CSI Camera

On the Raspberry Pi, check the camera name in the **Raspberry Pi shell**:

```bash
rpicam-hello --list-cameras
```

Use the full camera name reported by `rpicam-hello`. A typical source block looks like this:

```json
"libcamerasrc camera-name=\"/base/axi/pcie@1000120000/rp1/i2c@80000/imx708@1a\" !",
"video/x-raw,format=RGB,width=1536,height=864,framerate=60/1 !",
"videoconvert ! video/x-raw,format=BGRA !",
```

If your camera name is different, replace the value inside `camera-name="..."`.

## 5. Run The Pipeline Again

Run in the **Docker shell**:

```bash
./tools/pek-menu -l
```

Or run the pipeline explicitly:

```bash
./tools/pek-menu 05-full-onnx-raspicam
```

For a USB camera exposed as `/dev/video0`, run:

```bash
./tools/pek-menu 06-full-onnx-usb-cam
```

Open the web UI:

```text
http://localhost:9999
```

For Raspberry Pi:

```text
http://raspberrypi.local:9999
```

Enable one model in the **AI Models** panel.

Expected result: the browser shows camera input instead of the checked-in sample media.

## If The Camera Does Not Work

- Confirm the sample-media pipeline worked before the camera edit.
- Confirm the camera is visible with `v4l2-ctl --list-devices` or `rpicam-hello --list-cameras`.
- Confirm the camera path or camera name in the JSON matches the detected device.
- Keep the output format conversion to `BGRA`; PEK video elements expect that format in the normal path.
- Stop the running pipeline with `Ctrl+C` before starting it again.

[Back to How-To Guides](/how-to)
