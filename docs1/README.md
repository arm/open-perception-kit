# Arm Perception Kit CLI quick start

The Arm Perception Kit helps Raspberry Pi developers get from setup to 
edge-vision inference without building the whole perception stack from scratch. 
It gives you a fast path from a ready Raspberry Pi 5 to visible inference and 
a starting point for a vision application.

It uses a container-based workflow with a packaged pipeline, browser viewer,
model controls, debug signals, and output paths you can adapt for your own
application.

**Note:** This developer preview is for evaluation, early application
development, and feedback.

![Example WebRTC viewer showing sample video inference, model controls, performance metrics, and debug log](./static/img/10-browser-ui.png)

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

#### Alternative methods

Testing and development can be done using any computer where Docker is installed. 
Use the following links if you do not wish to deploy to Raspberry Pi 5:
* [For Windows](todo_cli_windows.md)
* [For Mac](todo_cli_mac.md)
* [For Linux](todo_cli_linux.md)


### 1. Connect and confirm the target Pi

#### 1.1 Start the SSH session

Open a terminal on the host machine, then run:

```bash
ssh <raspberry-pi-username>@<raspberry-pi-hostname-or-ip>
```

Use `raspberrypi.local` if it resolves to the target Pi you prepared. Otherwise,
use the target Pi IP address.

> **Expected outcome:** the host machine opens a shell on the target Pi.

#### 1.2 Check the target Pi

Before installing packages, check that the SSH session is on the expected
target Pi:

```bash
cat /proc/device-tree/model
grep VERSION_CODENAME /etc/os-release
uname -m
sudo -v
```

> **Expected outcome:** the output shows Raspberry Pi 5,
> `VERSION_CODENAME=trixie`, and `sudo -v` completes after asking for your
> password if needed.

### 2. Install Docker and target Pi base packages

#### 2.1 Install Docker Engine and Docker Compose

On the target Pi, follow Docker's Debian installation guide:
[Install Docker Engine on Debian](https://docs.docker.com/engine/install/debian/).

When you reach the Docker install command, make sure it includes
`docker-compose-plugin`. Later steps use `docker compose`.

#### 2.2 Install target Pi base packages

<!--
TODO@ibori: shall be reviewed, takes forever and I am not sure, we need all of those packages
-->

Run on the target Pi:

```bash
sudo apt-get update
sudo apt-get install -y git v4l-utils raspi-utils-core raspi-utils-dt rpicam-apps libcamera-dev libcamera-doc libcamera-tools gstreamer1.0-tools gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-gl gstreamer1.0-libcamera libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev ffmpeg cmake libcairo2-dev libssl-dev
```

### 3. Check Docker, get the repository, and prepare the container config

#### 3.1 Run the setup checks and clone

Run on the target Pi:

```bash
cd ~ &&
docker info &&
docker --version &&
docker compose version &&
git --version &&
if [ -d perception-kit/.git ]; then
  cd perception-kit
else
  git clone https://github.com/Arm-Debug/amp-dev-forge.git perception-kit &&
  cd perception-kit
fi &&
bash .devcontainer/platform_init.sh pek-dev-rpi5-h8
```

**Note:** The repository download can take a few minutes.

If `docker info` reports a permission error, run these commands, reconnect, and
rerun step 3:

```bash
sudo usermod -aG docker "$USER"
exit
ssh <raspberry-pi-username>@<raspberry-pi-hostname-or-ip>
```

> **Expected outcome:** Docker prints system details, Docker, Docker Compose,
> and Git print versions, the repository is available at `~/perception-kit`, and
> Docker Compose overrides are generated for the target Pi.

<!--
We should not use the term 'devcontainer' for the CLI path. 
Devcontainer is a VS Code term. "Create and start the Docker Development Container"
-->
### 4. Create and start the devcontainer

This creates the kit environment.

#### 4.1 Start the devcontainer

Run on the target Pi:

<!--
TODO@ibori: we (developers) should make these command shorter
-->
```bash
cd ~/perception-kit && 
docker compose \
    -f .devcontainer/compose.devcont.yaml \
    -f .devcontainer/docker-compose.devcont.video.yaml \
    -f .devcontainer/docker-compose.devcont.audio.yaml \
    -f .devcontainer/docker-compose.devcont.npu.yaml \
    -f .devcontainer/docker-compose.devcont.shared_memory.yaml up -d --build pek-dev-rpi5-h8 && 
docker ps --filter name=perception-experience-kit-rpi5 --format 'table {{.Names}} {{.Status}}'
```

**Note:** The first start can take several minutes while Docker builds the
container image. Leave the terminal running unless it prints a blocking error.

> **Expected outcome:** Docker shows a running container named
> `perception-experience-kit-rpi5`.

### 5. Build the kit inside the devcontainer

This builds the kit inside that environment.

#### 5.1 Run setup and build the kit

Run on the target Pi:

<!--
TODO@ibori: we (developers) should make these command shorter
-->

```bash
docker exec perception-experience-kit-rpi5 bash -lc '
  set -e
  cd /work
  ./.devcontainer/devsetup.sh
  test -f /work/data/videos/GettyImages-1140581459.mov || /work/scripts/download-data.sh
  test -f /work/data/videos/GettyImages-1140581459.mov
  /work/scripts/build-elements.sh debug false
  test -x /work/tools/pek-menu
  echo "Pipeline launcher is ready at /work/tools/pek-menu"
'
```

> **Expected outcome:** setup and kit build complete without a blocking error,
> and the terminal prints `Pipeline launcher is ready at /work/tools/pek-menu`.

**Note:** The first build can take several minutes. Leave the terminal running
unless it prints a blocking error.

### 6. Run your first pipeline and confirm inference

A pipeline is a saved runtime preset. It defines the input source, available
model controls, and where the result is shown.

#### 6.1 Start the pipeline

Run on the target Pi:

<!--
TODO@ibori: we (developers) should make these command shorter
-->
```bash
docker exec -it perception-experience-kit-rpi5 bash -lc 'cd /work && exec /work/tools/pek-menu 01-full-onnx'
```

Keep this terminal running. The command starts your first pipeline and runs
until you stop it with `Ctrl+C`.

> **Expected outcome:** the terminal prints the generated `gst-launch-1.0`
> command and the pipeline keeps running.

The target Pi serves the WebRTC inference viewer on port `9999`. Open it from
the host machine while the pipeline terminal keeps running.

#### 6.2 Open the WebRTC viewer

On the host machine, open:

```text
http://<raspberry-pi-ip-address>:9999
```

You can use the hostname instead if it resolves reliably on your network:
`http://<raspberry-pi-hostname>:9999`.

> **Expected outcome:** the viewer loads and shows the sample video stream,
> connection status, controls, the **Pipeline Output Model** panel, and the
> debug log.

#### 6.3 Check YoloV11

In the WebRTC browser viewer, find **YoloV11** in the **Pipeline Output
Model** panel. If the toggle is off, enable it.

> **Expected outcome:** YoloV11 identifies objects in the stock video stream by
> drawing detection overlays in the viewer.

![Final WebRTC success view showing inference overlays on the sample video stream](./static/img/10-browser-ui.png)

Congratulations, you have run your first Perception Kit pipeline!

## For VS Code users 

Pipeline testing and development are fully supported in Visual Studio Code (VS Code)
Follow the links below for detailed instructions:
* [Raspberry Pi 5](todo_rpi5_vscode.md)
* [Windows](todo_vscode_windows.md)
* [Mac](todo_vscode_mac.md)
* [Linux](todo_vscode_linux.md)


## After first success

Pick your next step.

<!--
TODO@ibori:
I think the first column of the table should contain the links. I saw one user couldn't find the link in the table.
-->
| Goal |  What it does |
| --- | --- |
| [Use your own input or output path](how-to/change-source-sink.md) | Keep the known pipeline and change the input or output. |
| [Use live camera input](tutorials/run-camera-inference.md) | Move from packaged media to a USB or Raspberry Pi camera. |
| [Add or adapt a model and OpChain](how-to/add-model-opchain.md) | Change the model after the source and output path work. |
| [Feed inference into an application](how-to/use-output-in-app.md) | Capture inference output for downstream logic. |
| [Use Hailo acceleration](tutorials/run-hailo-inference.md) | Add accelerator hardware. |
| [Understand pipelines, models, and outputs](start-here/how-the-kit-works.md) | Read the pipeline, model, and output concepts when you need more detail. |

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

For more symptoms, use [Troubleshooting](troubleshooting/index.md).
