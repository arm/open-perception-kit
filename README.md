# Arm Perception Kit CLI quick start

[![Python Dependency Audit](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/python-dependency-audit.yml/badge.svg?branch=main&event=schedule)](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/python-dependency-audit.yml)

The Arm Perception Kit helps Raspberry Pi developers get from setup to 
edge-vision inference without building the whole perception stack from scratch. 
It gives you a fast path from a ready Raspberry Pi 5 to visible inference and 
a starting point for a vision application.

It uses a container-based workflow with a packaged pipeline, browser viewer,
model controls, debug signals, and output paths you can adapt for your own
application.

**Note:** This developer preview is for evaluation, early application
development, and feedback.

![Example WebRTC viewer showing sample video inference, model controls, performance metrics, and debug log](docs/static/img/10-browser-ui.png)

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

**Note 1: the name of the repo will be changed**
**Note 2: this works only when public repository is released**

```bash
git clone https://github.com/Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

### 2. Install prerequisites, build and start the Docker Container

Enter the `amp-dev-forge` folder in the terminal and run:

```bash
./quick_start
```

### 3. Enter the container command line

```bash
./enter_cli
```
> **Expected outcome:** The prompt shows `devgoblin` 

#### 3.1 Optional: Download the stock videos

From the container shell, run:

```bash
./download_videos
```

### 4. Build PEK inside the Container

From the container shell, run:

```bash
./build
```
> **Expected outcome:** setup and kit build complete without a blocking error,
> and the terminal prints `Pipeline launcher is ready at /work/tools/pek-menu`.

### 5. Run your first pipeline and confirm inference

#### 5.1 Start the inference pipeline inside the Container

```bash
./run
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
> connection status, controls, the **AI Models** panel, and the
> debug log.

#### 5.3 Check YoloV11

In the WebRTC browser viewer, find **YoloV11** in the **Pipeline Output
Model** panel. If the toggle is off, enable it.

> **Expected outcome:** YoloV11 identifies objects in the stock video stream by
> drawing detection overlays in the viewer.

![Final WebRTC success view showing inference overlays on the sample video stream](docs/static/img/10-browser-ui.png)

Congratulations, you have run your first Perception Kit pipeline!

## For VS Code users 

Pipeline testing and development are fully supported in Visual Studio Code (VS Code)
Follow the links below for detailed instructions:

* [Raspberry Pi 5](docs/public/raspberry-pi-quick-start.md)
* [Windows](docs/public/windows-quick-start.md)
* [Mac](docs/public/macos-quick-start.md)
* [Linux](docs/public/linux-quick-start.md)


## After first success

Pick your next step.

| Goal |  What it does |
| --- | --- |
| [Use your own input or output path](docs/public/media-input.md) | Keep the known pipeline and change the input or output. |
| [Use live camera input](docs/public/camera-input.md) | Move from packaged media to a USB or Raspberry Pi camera. |
| [Add or adapt a model and OpChain](docs/public/bring-your-model.md) | Change the model after the source and output path work. |
| [**Coming Soon:** Feed inference into an application](docs/public/use-output-in-app.md) | Capture inference output for downstream logic. |
| [Use Hailo acceleration](docs/public/run-hailo-inference.md) | Add accelerator hardware. |
| [Understanding the repository structure](docs/public/structural-basics.md) | How to get started with new components |
| [Pipeline basics](docs/public/runtime-basics.md) | Learn about inference pipeline principles  |
| [Custom postprocessing](docs/public/custom-postprocessing.md) |    |



## If something goes wrong

| Symptom | Do this first |
| --- | --- |
| SSH fails from the host machine | Check the target Pi hostname or IP address, then retry with the IP address. |
| `docker info` fails | Confirm Docker Engine is installed and running from Docker's Debian installation guide. If it reports a permissions error, run `sudo usermod -aG docker "$USER"`, reconnect, and try again. |
| Docker Compose cannot find the service | Rerun `bash .devcontainer/platform_init.sh pek-dev-rpi5-h8`, then rerun the container start command. |
| Build fails | Fix the first missing package, permission, or container error shown in the build output. |
| Pipeline exits immediately | Rerun `docker exec -it perception-experience-kit-rpi5 bash -lc 'cd /work && /work/tools/pek-menu 01-full-onnx'` and inspect the first missing plugin, model, or file. |
| Viewer does not load | Keep the pipeline terminal running, use the target Pi IP address, and check port `9999`. |
| A model produces no overlay | Confirm the model and any upstream dependencies are enabled, then check the debug log or model state in the viewer. |

