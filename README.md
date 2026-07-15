# Arm Perception Kit

[![Python Dependency Audit](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/python-dependency-audit.yml/badge.svg?branch=main&event=schedule)](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/python-dependency-audit.yml)
[![Docker Scout Image Audit](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/docker-scout-image-audit.yml/badge.svg?branch=main&event=schedule)](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/docker-scout-image-audit.yml?query=branch%3Amain+event%3Aschedule)
[![Workflow Dependency Freshness](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/workflow-audit.yml/badge.svg?branch=main&event=schedule)](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/workflow-audit.yml?query=branch%3Amain+event%3Aschedule)

Perception Kit provides containerised computer-vision pipelines, models, a
browser viewer, and development tools for Arm64 Linux targets.

## Get started

This project is deployed using Topo, which allows it to be built from a fast
development machine and deployed remotely to an arm64 development board, like
a raspberry pi 5.

First, clone the repository locally to your development machine.
```bash
git clone https://github.com/Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```


[Install Topo](https://github.com/arm/topo) by following its installation instructions.

### Deploy locally

Clone this repository on your computer:

instructions, then deploy the default sample-video pipeline:

```bash
topo deploy --target <pi-ip>
```

When the deployment has started, open:

```text
http://localhost:9999
```

> **macOS:** In Docker Desktop, enable **Settings > Resources > Network >
> Enable host networking** before starting the deployment.

### Deploy to a remote target

Check the target is set up, and resolve any issues raised by topo.
```bash
topo health --target <pi-ip>
```
Then, deploy when ready.
```bash
topo deploy --target <pi-ip>
```

When the deployment has finished, open:
```text
http://<pi-ip>:9999
```

### Deploy locally without topo.
Topo projects are backwards compatible with normal compose projects. You may wish
to use standard docker compose up.

```bash
docker compose up --build
```

## Using Raspberry Pi Camera

Connect the camera to the Pi, restart the device, then enable the optional camera dependencies and
select the Pi camera pipeline:

```bash
PEK_PICAMERA=enabled PEK_PIPELINE=05-full-onnx-raspicam \
  topo deploy --target <pi-ip>
```

For a USB camera exposed as `/dev/video0` on the target:

```bash
PEK_PIPELINE=06-full-onnx-usb-cam topo deploy --target <pi-ip>
```

![PEK browser viewer showing sample-video inference](docs/public/static/img/10-browser-ui.png)


## Develop Locally

Use the VS Code Dev Container for source changes, debugging, and tests. Install
Visual Studio Code and its **Dev Containers** extension, then open the
repository:

```shell
code .
```

In VS Code:

1. Open the Command Palette.
2. Run **Dev Containers: Reopen in Container**.
3. Select **PC perception-experience-kit**.
4. Wait for VS Code to build and open the development container.

Use **Terminal > Run Task > 00 Build Project** to build PEK. Then use
**Terminal > Run Task > 00 Run project and select pipeline** and select
`01-full-onnx`.

Open `http://localhost:9999` while the pipeline is running.

Detailed setup guides:

- [macOS](docs/public/getting-started/macos-quick-start.md)
- [Linux](docs/public/getting-started/linux-quick-start.md)
- [Windows with WSL](docs/public/getting-started/windows-quick-start.md)
- [Raspberry Pi 5](docs/public/getting-started/raspberry-pi-quick-start.md)

## Documentation

- [Camera input](docs/public/how-to/camera-input.md)
- [Media input](docs/public/how-to/media-input.md)
- [Bring your own model](docs/public/how-to/bring-your-model.md)
- [Repository structure](docs/public/concepts/structural-basics.md)
- [Runtime basics](docs/public/concepts/runtime-basics.md)
- [Custom postprocessing](docs/public/how-to/custom-postprocessing.md)
- [Performance measurement](docs/public/how-to/performance-measurement.md)
