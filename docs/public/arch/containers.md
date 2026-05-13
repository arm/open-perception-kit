---
sidebar_position: 4
sidebar_label: Containers
---

# Containerized Execution Model
## About

The system is designed around a container-first development and deployment model.

Developers pull the repository and build the development container.
This provides a consistent and reproducible working environment across platforms.

The container executes the GStreamer pipeline, including inference workloads.

---

## Development Model

- The repository defines the full build environment.
- The container encapsulates all dependencies.
- The same environment is used across Linux, macOS, Windows, Raspberry Pi 5, and other devices.
- This eliminates host-specific configuration drift.

The repository currently defines three main container workflows:

- `pek-dev-base` for the normal host side container workflow
- `pek-dev-rpi5-h8` for the Raspberry Pi 5 + Hailo 8 AI HAT workflow
- `pek-dev-rpi5-h10` for the Raspberry Pi 5 + supported Hailo 10 accelerator workflow

The host side container uses bridge networking with published ports.
The remote host containers use host networking so the web UI and docs are exposed directly on the remote host.

While containers provide reproducibility, they introduce runtime challenges:

- The container is isolated from the host display server.
- Video/audio playback inside the container is non-trivial.
- Direct GPU / display integration may be constrained.
- UDP-based audio/video streaming is often laggy and unstable.
- Media debugging becomes difficult with traditional forwarding approaches.

## Host-Side Device Passthrough

Before a host side container or remote host container starts, `.devcontainer/platform_init.sh` runs on the host or remote host.
That script calls `scripts/private/dev-init.sh`, which generates the docker-compose override files used for:

- camera passthrough
- audio passthrough
- NPU device passthrough
- shared-memory and DMA-related mounts

The generated override filenames follow the selected container kind, for example `.devcontainer/docker-compose.devcont.video.yaml` and `.devcontainer/docker-compose.devcont.npu.yaml`.
`devices.env` is generated alongside them so the selected service sees the matching host device environment.

This means remote host container setup is not only a static `Dockerfile` choice.
It is a combination of:

- the selected container service
- host or remote host device discovery
- generated docker-compose override files

## Raspberry Pi Hailo Split

The Raspberry Pi workflow is now split by accelerator generation.

- `pek-dev-rpi5-h8` installs the Hailo 8-oriented user-space stack together with the camera packages used by the Pi pipelines.
- `pek-dev-rpi5-h10` installs the Hailo 10 user-space stack together with the same camera packages.

Hailo 8 and Hailo 8L compiled model files are not interchangeable. Use the Hailo 8L model folders and `03-full-onnx-hailo8l.json` preset only with Hailo 8L hardware.

For the Hailo 10 path, the container intentionally does not install kernel driver packages.
Those host-level packages, such as `h10-hailort-pcie-driver`, need to stay on the remote host because they depend on host kernel and module tooling.

---

## `peksink`: WebRTC-Based Output

To solve these issues, the system introduces **`peksink`**.

![WebRTC utilization](../../static/img/webrtc.png)

The element is called `peksink`.
`peksink` provides a web endpoint inside the container that publishes media.
The host web browser connects to this endpoint and renders the media stream.

- Streams media using WebRTC.
- Provides low-latency video and audio output.
- Avoids unstable UDP streaming.
- Works reliably across host platforms.
- Enables browser-based pipeline control.

---

## WebSocket Control Interface

The browser communicates with the container using WebSocket control commands.

This allows:

- Pipeline start/stop control
- Triggering actions
- Service management

Media transport and control signaling are cleanly separated.

---

## `ampsource` (Planned)

`ampsource` would extend the architecture in the opposite direction.
The system routes the browser camera and microphone data into the container.
Web-based video/audio streams can also be routed into the container as a pipeline source.
Media is streamed into the pipeline.
This enables different use cases where the user can use the device with a single browser connection.

Example use cases:

- Video conference with neural network inferences
- Real-time translated web radio

---

## Architectural Benefits

- Containerized reproducibility
- WebRTC-based stable media streaming
- No host display dependencies
- Clean separation of runtime and presentation layers
- Browser-based universal interface
- Edge-ready deployment model

This model allows development, testing, and deployment using the same architecture,
from local machines to Raspberry Pi 5 and other edge devices.
