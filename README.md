# Open Perception Kit CLI quick start

[![Python Dependency Audit](https://github.com/arm/open-perception-kit/actions/workflows/python-dependency-audit.yml/badge.svg?branch=develop&event=schedule)](https://github.com/arm/open-perception-kit/actions/workflows/python-dependency-audit.yml?query=branch%3Adevelop+event%3Aschedule)
[![Docker Scout Image Audit](https://github.com/arm/open-perception-kit/actions/workflows/docker-scout-image-audit.yml/badge.svg?branch=develop&event=schedule)](https://github.com/arm/open-perception-kit/actions/workflows/docker-scout-image-audit.yml?query=branch%3Adevelop+event%3Aschedule)
[![Workflow Dependency Freshness](https://github.com/arm/open-perception-kit/actions/workflows/workflow-audit.yml/badge.svg?branch=develop&event=schedule)](https://github.com/arm/open-perception-kit/actions/workflows/workflow-audit.yml?query=branch%3Adevelop+event%3Aschedule)

The workflow dependency freshness badge links to the workflow runs, where each run publishes a simple Markdown report and lightweight JSON snapshot in the `workflow-dependency-freshness` artifact.

The Open Perception Kit helps Raspberry Pi developers get from setup to
edge-vision inference without building the whole perception stack from scratch.
It gives you a fast path from a ready Raspberry Pi 5 to visible inference and
a starting point for a vision application.

It uses a container-based workflow with a packaged pipeline, browser viewer,
model controls, debug signals, and output paths you can adapt for your own
application.

At a high level, it combines:

- GStreamer-based media pipeline integration
- an Op-based execution model for preprocessing, inference, and postprocessing
- schema-defined FrameResults that downstream elements can render, track, or publish

**Note:** This developer preview is for evaluation, early application
development, and feedback.

![Example WebRTC viewer showing sample video inference, model controls, performance metrics, and debug log](docs/public/static/img/10-browser-ui.png)

## Quick start: first inference on Raspberry Pi 5

### What to expect

Plan for around 45 minutes from starting these steps with the prerequisites
ready to first inference.

This guide uses a host machine and a target Pi:

- The host machine: the computer used for SSH and the browser viewer.
- The target Pi: the Raspberry Pi 5, where you clone the repository, build the
  container, build the kit, and run your first pipeline.

In this quick start, you will:

- Connect to and confirm the target Pi.
- Clone the repository onto the target Pi.
- Build and start the container.
- Build the kit inside the container.
- Run the first pipeline.
- Open the browser viewer and confirm inference.

### Starting prerequisites

Have these on the host machine before you start:

- Windows, macOS, or Linux.
- Network access from the host machine to the target Pi.

Use this target Pi setup before you start:

- Raspberry Pi 5 with at least 8GB RAM and 64-bit Raspberry Pi OS based on
  Debian Trixie.
- SSH enabled on the Raspberry Pi, with a username and password you can use from
  the host machine.
- Known target Pi hostname or IP address.
- Permission to run `sudo` on the target Pi.
- Internet access from the target Pi to GitHub, package repositories, and
  container or source locations used during the first container build.
- An optional read-only Hugging Face `HF_TOKEN` for private or gated OPK
  models.

### 1. Connect to the target Pi

#### 1.1 Start the SSH session

Open a terminal on the host machine, then run:

```bash
ssh <raspberry-pi-username>@<raspberry-pi-hostname-or-ip>
```

Use `raspberrypi.local` if it resolves to the target Pi you prepared. Otherwise,
use the target Pi IP address.

> **Expected outcome:** the host machine opens a shell on the target Pi.

#### 1.2 Clone the repository 

In the Raspberry Pi 5 terminal run:

```bash
git clone https://github.com/arm/open-perception-kit.git
cd open-perception-kit
```

### 2. Install prerequisites, build and start the Docker Container

Enter the `open-perception-kit` folder in the terminal and run:

```bash
./scripts/quick_start.sh
```

When `HF_TOKEN` is unset, accessible public models download anonymously. Export
`HF_TOKEN` before the quick-start when the build also needs private or gated
models:

```bash
export HF_TOKEN="hf_your_token_here"
./scripts/quick_start.sh
```

Compose exposes the value only to the Docker model-download build step. The
image contains each successfully downloaded model file, but neither the token
nor a runtime Hugging Face credential. Failed downloads are logged and skipped,
so the image build still succeeds. A pipeline that references a missing model
fails while its OpChain starts, even when that `opkinfer` has `active=false`.

### 3. Enter the container command line

```bash
./scripts/enter_cli.sh
```
> **Expected outcome:** The prompt shows `dev`

### 4. Build OPK inside the Container

From the container shell, run:

```bash
./scripts/build.sh
```
> **Expected outcome:** setup and kit build complete without a blocking error,
> and the terminal prints:
>
> ```text
> Pipeline launcher is ready at /work/tools/opk-menu
> Run it with:
>   ./scripts/run.sh
> ```

### 5. Run your first pipeline and confirm inference

#### 5.1 Start the inference pipeline inside the Container

```bash
./scripts/run.sh
```

Keep this terminal running. The command starts your first pipeline and runs
until you stop it with `Ctrl+C`.

> **Expected outcome:** the terminal prints the generated `gst-launch-1.0`
> command and the pipeline keeps running.

The target Pi serves the WebRTC inference viewer on port `9999`. Open it from
the host machine while the pipeline terminal keeps running.

#### 5.2 Open the WebRTC viewer

On the host machine, open:

```text
http://<raspberry-pi-ip-address>:9999
```

You can use the hostname instead if it resolves reliably on your network:
`http://<raspberry-pi-hostname>:9999`.

> **Expected outcome:** the viewer loads and shows the sample video stream,
> connection status, controls, the **Model Selector** panel, and the
> debug log.

#### 5.3 Check YoloV11

In the WebRTC browser viewer, find **YoloV11** in the **Model Selector** panel.
If the toggle is off, enable it.

> **Expected outcome:** YoloV11 identifies objects in the stock video stream by
> drawing detection overlays in the viewer.

![Final WebRTC success view showing inference overlays on the sample video stream](docs/public/static/img/10-browser-ui.png)

Congratulations, you have run your first Open Perception Kit pipeline!

## Platform-specific setup

Use these guides for platform prerequisites and command-line or VS Code setup:

* [Raspberry Pi 5](docs/public/getting-started/raspberry-pi-quick-start.md)
* [Windows](docs/public/getting-started/windows-quick-start.md)
* [Mac](docs/public/getting-started/macos-quick-start.md)
* [Linux](docs/public/getting-started/linux-quick-start.md)


## After first success

Pick your next step.

| Goal |  What it does |
| --- | --- |
| [Use your own input or output path](docs/public/how-to/media-input.md) | Keep the known pipeline and change the input or output. |
| [Use live camera input](docs/public/how-to/camera-input.md) | Move from packaged media to a USB or Raspberry Pi camera. |
| [Use a binary release](docs/public/getting-started/binary-release.md) | Integrate the six packaged GStreamer plugins without an OPK loader wrapper. |
| [Add or adapt a model and OpChain](docs/public/how-to/bring-your-model.md) | Change the model after the source and output path work. |
| [**Coming Soon:** Feed inference into an application](docs/public/how-to/use-output-in-app.md) | Capture inference output for downstream logic. |
| [Understanding the repository structure](docs/public/concepts/structural-basics.md) | How to get started with new components |
| [Pipeline basics](docs/public/concepts/runtime-basics.md) | Learn about inference pipeline principles  |
| [Custom postprocessing](docs/public/how-to/custom-postprocessing.md) | Inference result postprocessing  |
| [Python script Op](docs/public/how-to/python-script-op.md) | Stateful scripting with FrameResults and tensors |
| [Performance Measurement](docs/public/how-to/performance-measurement.md) | Measure the pipeline performance with Performix |



## If something goes wrong

| Symptom | Do this first |
| --- | --- |
| SSH fails from the host machine | Check the target Pi hostname or IP address, then retry with the IP address. |
| `docker info` fails | Confirm Docker Engine is installed and running from Docker's Debian installation guide. If it reports a permissions error, run `sudo usermod -aG docker "$USER"`, reconnect, and try again. |
| Docker Compose cannot find the service | Rerun `./scripts/quick_start.sh` to regenerate the container configuration. |
| Build fails | Fix the first missing package, permission, or container error shown in the build output. |
| Pipeline exits immediately | Rerun `./scripts/run.sh yolo26-onnx` and inspect the first missing plugin, model, or file. |
| Viewer does not load | Keep the pipeline terminal running, use the target Pi IP address, and check port `9999`. |
| A model produces no overlay | Confirm the model and any upstream dependencies are enabled, then check the debug log or model state in the viewer. |

## Advanced

The quick-start scripts are the recommended first path. Use the workflows below
when you need direct deployment control or remote deployment with Topo.

### Build a deployment container directly with Docker Compose

Generate a cache key before invoking Compose. Export a token too only when
private or gated models are needed:

```bash
export HF_DOWNLOAD_CACHEBUST="$(./scripts/private/generate-hf-download-cachebust.sh)"
# Optional: export HF_TOKEN="hf_your_token_here"
docker compose up --build
```

Generate a fresh cache key before every direct Compose build. The model stage
rejects builds when the key is omitted, preventing an authenticated build from
silently reusing a cached anonymous model layer.

### Deploy with Topo

Install [Topo](https://github.com/arm/topo) on your development machine, then
check that the target is ready:

```bash
topo health --target <raspberry-pi-ip-address>
```

Deploy the default sample-video pipeline from the repository root:

```bash
export HF_DOWNLOAD_CACHEBUST="$(./scripts/private/generate-hf-download-cachebust.sh)"
HF_TOKEN="" topo deploy --target <raspberry-pi-ip-address>
```

For a private or gated model, export your Hugging Face token instead:

```bash
export HF_TOKEN="hf_your_token_here"
export HF_DOWNLOAD_CACHEBUST="$(./scripts/private/generate-hf-download-cachebust.sh)"
topo deploy --target <raspberry-pi-ip-address>
```

Topo forwards the value through the same read-only Compose secret.

When the deployment has started, open:

```text
http://<raspberry-pi-ip-address>:9999
```

For a Raspberry Pi camera, connect the camera and restart the Pi before
deploying:

```bash
OPK_PICAMERA=enabled OPK_PIPELINE=full-onnx-raspicam \
  topo deploy --target <raspberry-pi-ip-address>
```

For a USB camera exposed as `/dev/video0` on the target:

```bash
OPK_PIPELINE=full-onnx-usb-cam \
  topo deploy --target <raspberry-pi-ip-address>
```
