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

The repository currently defines three main development targets:

- `amp-dev-base` for the normal PC devcontainer workflow
- `amp-dev-rpi5-h8` for Raspberry Pi 5 + Hailo 8 / AI HAT+ workflow
- `amp-dev-rpi5-h10` for Raspberry Pi 5 + Hailo 10 / AI HAT+ 2 workflow

The PC container uses bridge networking with published ports.
The Raspberry Pi containers use host networking so the web UI and docs are exposed directly on the Pi host.

While containers provide reproducibility, they introduce runtime challenges:

- The container is isolated from the host display server.
- Video/audio playback inside the container is non-trivial.
- Direct GPU / display integration may be constrained.
- UDP-based audio/video streaming is often laggy and unstable.
- Media debugging becomes difficult with traditional forwarding approaches.

## Host-Side Device Passthrough

Before a devcontainer starts, `.devcontainer/platform_init.sh` runs on the host.
That script calls `scripts/private/dev-init.sh`, which generates the docker-compose override files used for:

- camera passthrough
- audio passthrough
- NPU device passthrough
- shared-memory and DMA-related mounts

The generated override filenames follow the selected container kind, for example `.devcontainer/docker-compose.devcont.video.yaml` and `.devcontainer/docker-compose.devcont.npu.yaml`.
`devices.env` is generated alongside them so the selected service sees the matching host device environment.

This means Raspberry Pi container setup is not only a static `Dockerfile` choice.
It is a combination of:

- the selected devcontainer service
- host-side device discovery
- generated docker-compose override files

## Raspberry Pi Hailo Split

The Raspberry Pi workflow is now split by accelerator generation.

- `amp-dev-rpi5-h8` installs the Hailo 8-oriented user-space stack together with the camera packages used by the Pi pipelines.
- `amp-dev-rpi5-h10` installs the Hailo 10 user-space stack together with the same camera packages.

For the Hailo 10 path, the container intentionally does not install kernel driver packages.
Those host-level packages, such as `h10-hailort-pcie-driver`, need to stay on the Raspberry Pi host because they depend on host kernel and module tooling.

---

## `ampsink`: WebRTC-Based Output

To solve these issues, the system introduces **`ampsink`**.

![WebRTC utilization](../../static/img/webrtc.png)

The element is called `ampsink`.
`ampsink` provides a web endpoint inside the container that publishes media.
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
