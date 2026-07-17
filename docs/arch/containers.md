---
sidebar_position: 4
sidebar_label: Containers
---

# Containerized Execution Model

OPK uses a container-first workflow to keep build tools,
runtime dependencies, and device integration consistent across host platforms and
Raspberry Pi targets.

## Development Model

The repository defines the build environment and container services. The main
workflows are:

- `pek-dev-base` for the normal host-side container workflow.
- `pek-dev-rpi5-h8` for Raspberry Pi 5 with Hailo 8 or Hailo 8L hardware.
- `pek-dev-rpi5-h10` for Raspberry Pi 5 with supported Hailo 10 hardware.

Host-side containers use bridge networking with published ports. Remote host
containers use host networking so the web UI and docs are exposed directly on
the remote host.

## Device Passthrough

Before a host-side or remote-host container starts, `.devcontainer/platform_init.sh`
runs on the host. It calls `scripts/private/dev-init.sh`, which generates
Docker Compose overrides for camera, audio, NPU, shared-memory, and DMA-related
mounts.

The selected container service, discovered host devices, generated compose files,
and `devices.env` together define the final runtime environment.

## Raspberry Pi Hailo Split

The Raspberry Pi workflow is split by accelerator generation. Hailo 8/Hailo 8L
model files are not interchangeable with Hailo 10 model files. Use the matching
container service, model folders, and presets for the attached hardware.

The Hailo 10 container intentionally does not install kernel driver packages;
host-level packages such as `h10-hailort-pcie-driver` remain on the Raspberry Pi
because they depend on host kernel and module tooling.

## Media Output From Containers

Direct display/audio output from inside a container is fragile across platforms.
OPK uses `peksink` to expose media through WebRTC and browser-based control
instead of relying on host display forwarding or UDP streaming. See
[peksink](elements/peksink.md).

## Architectural Benefits

- Reproducible build and runtime environment.
- Explicit host-device discovery and passthrough.
- Browser-based media output without host display dependencies.
- Similar development and deployment shape across local machines and edge devices.
