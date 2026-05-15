---
sidebar_position: 1
sidebar_label: Get started
slug: /
---

# Perception Experience Kit (PEK)

Perception Experience Kit (PEK) enables you to run AI-powered
media-processing pipelines in a reproducible containerised environment.

In this guide, media primarily refers to video frames sourced from an image,
video file, or camera feed.

A PEK pipeline can:

- read media input
- run one or more AI models on the media
- process the results
- display the output in a browser

For example, a pipeline might detect objects in a video stream and display the
detection results in real time.

## Get started

### 1. Set up PEK and run the sample media demo

Start with the guide for the machine where PEK will run. Each quick start
includes the detailed setup checks, build steps, and first browser success check
using included sample media.

| PEK will run on... | Hardware you need | Software you need | Start here |
|---|---|---|---|
| Raspberry Pi 5 | Raspberry Pi 5, power supply, network connection.<br />(Optional) Camera: USB camera or Raspberry Pi CSI camera, such as Camera Module 3 or High Quality Camera. Not needed for the sample demo.<br />(Optional) Hailo AI HAT. Required for Hailo pipelines. | Raspberry Pi OS, VS Code, Remote SSH extension, Dev Containers extension. | [Raspberry Pi 5 Tutorial](raspberry-pi-quick-start.md) |
| Windows PC | Windows PC.<br />(Optional) Camera: supported camera input, if configured for WSL/container access. Not needed for the sample demo. | WSL with Ubuntu, Git in WSL, Docker Desktop with WSL integration, VS Code, Dev Containers extension. | [Windows Quick Start](windows-quick-start.md) |
| Linux PC | Linux PC.<br />(Optional) Camera: USB camera or other camera visible as a Linux video device. Not needed for the sample demo. | Git, Docker Engine, Docker Compose, VS Code, Dev Containers extension. | [Linux Quick Start](linux-quick-start.md) |
| Mac | Mac.<br />(Optional) Camera: supported camera input, if configured for container access. Not needed for the sample demo. | Git, Docker Desktop, VS Code, Dev Containers extension. | [macOS Quick Start](macos-quick-start.md) |

The first goal is to confirm that PEK can build, start, open the browser UI,
and show AI results on the included sample media.

The demo includes common perception model types such as object detection, face
detection, image classification, segmentation, OCR, and embeddings. You only
need to enable one model to confirm the first run works.

For a live camera first run, Raspberry Pi users can choose
`05-full-onnx-raspicam` for a Raspberry Pi camera or `06-full-onnx-usb-cam` for
a USB camera. Linux users with a USB camera exposed as `/dev/video0` can
choose `06-full-onnx-usb-cam`.

### 2. Switch to your own input

After the sample media demo works, use a camera, image, video file, or media
stream.

- Camera: [Use A Camera](camera-input.md)
- Image file: [Use Your Own Media](media-input.md#use-an-image-file)
- Video file: [Use Your Own Media](media-input.md#use-a-video-file)
- Media stream: [Use Your Own Media](media-input.md#use-a-media-stream)

### 3. Bring your own model

Add or adapt a model after the input and runtime flow are clear.

- [Bring Your Model](bring-your-model.md)

### 4. Customize postprocessing only if needed

Do this only when your model output does not match an existing PEK parser.

- [Custom Postprocessing](custom-postprocessing.md)

### 5. Use reference pages when you need context

Use these pages when you need to understand how PEK is organized:

- [Structural Basics](structural-basics.md)
- [Runtime Basics](runtime-basics.md)
- [Performance Measurement With Performix](performance-measurement.md)

## Optional Setup Pages

- [Raspberry Pi SSH Setup](raspberry-pi-ssh.md) - use this before the Raspberry Pi tutorial if you want to connect from your normal computer.
- [GitHub SSH Key Setup](github-ssh-key.md) - use this only if you need to clone from GitHub with an SSH URL.
