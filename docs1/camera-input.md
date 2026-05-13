# Use A Camera

Use this page after a quick start works with the checked-in sample media.

The first camera test should change only the input source. Keep the rest of the pipeline unchanged so you know any failure is related to camera input, not model or sink changes.

## 1. Choose A Pipeline File

Open one of these files in VS Code:

- `config/pipelines/01-full-onnx.json`
- `config/pipelines/02-full-onnx-hailo8.json`
- `config/pipelines/03-full-onnx-hailo8l.json`
- `config/pipelines/04-full-onnx-hailo10.json`

For the first camera test, start with `config/pipelines/01-full-onnx.json`.

## 2. Find The Current Source

At the top of the `pipeline` array, the checked-in sample source currently looks like this:

```json
"filesrc location=/work/data/videos/GettyImages-1140581459.mov !",
"decodebin !",
"videoconvert !",
"video/x-raw,format=BGRA !",
```

Replace only those source lines. Leave the `ampinfer`, `amptracker`, `ampperformance`, `amposd`, and `ampsink` lines as they are.

## 3. Use A USB Camera

On Raspberry Pi or Linux, check the USB camera path in the **host shell** or **Raspberry Pi shell**:

```bash
v4l2-ctl --list-devices
```

If your camera appears as `/dev/video0`, replace the source lines with:

```json
"v4l2src device=/dev/video0 ! \"image/jpeg,width=1280,height=720,framerate=60/1\" !",
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
./tools/amp-menu -l
```

Or run the pipeline explicitly:

```bash
./tools/amp-menu 01-full-onnx
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
- Keep the output format conversion to `BGRA`; AMP video elements expect that format in the normal path.
- Stop the running pipeline with `Ctrl+C` before starting it again.
