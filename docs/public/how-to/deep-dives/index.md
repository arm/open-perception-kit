---
sidebar_position: 1
sidebar_label: How-To
---

# AMP Development Forge How-To

This is the deep-dive setup and usage guide.

If you only want the shortest path to a first run, use the platform quick-guide pages instead.
If you want the fuller setup path and the next documentation hub after this tutorial, continue from here to [Engineering starting point](engineering.md).

## Terminology

* host - windows+WSL/Linux/mac PC
* host side container - containerized environment on the windows+WSL/Linux/mac PC host
* remote host - Raspberry Pi is the only supported remote host as of now.
* remote host container - containerized environment on the RPi host
* target - Only applicable during deployment when from a host (windows+WSL/Linux/mac PC) we deploy the container onto the target(Raspberry Pi at the moment)

## What will you learn from this documentation?

If you follow this guide successfully, you will learn how to:

- prepare a supported host or remote host for AMP
- clone the repository and open it in the expected container workflow
- build the project, run `amp-menu`, and start a first pipeline
- find the published endpoints and continue into the next engineering-focused documents

At the end of this guide, you should have a working AMP environment on your desk, a first pipeline running, and a clear path to the next deep-dive topics.

## Quick Overview
- **Goal:** Get AMP running locally or on a Raspberry Pi target
- **You'll need:** Docker, VS Code, Git, and an SSH key
- **Recommended first run:** ONNX pipeline

**Steps:**
1. [Clone the AMP repository](#clone-the-repository)
2. [Install dependencies](#host-side-dependencies)
3. [Open and build the project](#open-and-start-the-project) or [deploy with Topo](topo.md)

---

### Clone the repository
For PC development, clone the repository on your host.
For on-device Raspberry Pi 5 development, clone the repository on the remote host after setting up SSH successfully.
Alternatively you can develop on your PC and deploy to the target with Topo.
![AMP Development Forge repository root](../../../static/img/02-repo-root.png)

If you need a source archive instead of a Git clone, use the release page and download the compressed source package.

![GitHub release page showing the source code download](../../../static/img/01-repo-compressed.png)

```bash
git clone git@github.com:Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

![Terminal output after cloning the repository](../../../static/img/03-repo-clone.png)

## Host side dependencies

We currently support four hosts: Windows with WSL, Linux, macOS, and Raspberry Pi 5.
Install the required tools on your host in order to use the project.

> Besides the listed dependencies, additional tools are installed inside the development or deployment container.
Working directly on the host outside the container is not well supported at the moment. The project assumes container-managed dependencies.

### Windows+WSL
   * [WSL](https://learn.microsoft.com/en-us/windows/wsl/install)
   * [Git](https://git-scm.com/install/)
   * [Docker Desktop](https://www.docker.com/products/docker-desktop/)
   * [Visual Studio Code](https://code.visualstudio.com/download)
   * [VS Code Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)
   * [WSL USB Manager 5.7.0](https://github.com/nickbeth/wsl-usb-manager)

### Linux
   * [Git](https://git-scm.com/install/)
   * **Docker**
   * [Visual Studio Code](https://code.visualstudio.com/download)
   * [VS Code Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)
   * **v4l-utils**

The following command should help with this.

```bash
sudo apt-get update
sudo apt-get install -y git docker.io code v4l-utils
```

### Mac
   * [Git](https://git-scm.com/install/)
   * [Docker Desktop](https://www.docker.com/products/docker-desktop/)
      * Colima is not tested at the moment due to networking issues.
   * [Visual Studio Code](https://code.visualstudio.com/download)
   * [VS Code Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)
   * [Remote development extension](https://code.visualstudio.com/docs/remote/ssh)
      * The following permission shall be granted in Settings otherwise the remote connection will fail Privacy & Security -> Local Network : vscode

### Raspberry Pi 5
   * **Docker**
   * **Camera packages**
   * **Hailo 8 or Hailo 10 host stack**, depending on the target
   * **v4l-utils**
   * [Required device and required packages on the target](rpi5.md)
   * [Setup SSH connection](#ssh-setup)

Use `RPI5 H8 amp-dev-forge` for the Hailo 8 / AI HAT+ path.
Use `RPI5 H10 amp-dev-forge` for the Hailo 10 / AI HAT+ 2 path.
The Hailo 10 container expects the host-side Hailo 10 driver stack to already be installed.

### SSH setup
Before starting the DevContainer, ensure that the `ssh-agent` is running and that your GitHub private key has been added to it.
This can be done in several ways depending on your operating system. The setup for Linux and macOS is as follows:

```bash
# For bash users: add ssh-agent to your .profile
eval "$(ssh-agent -s)"
```

The above command starts the SSH agent, which automatically adds your default keys (for example, `.ssh/id_rsa`, `.ssh/id_dsa`, etc.) from the `.ssh` directory.

If you need to add a different key, use the following command:

```bash
ssh-add [private_key_filename]
```

For further information and a detailed tutorial check out the following tutorial: [Generating a new SSH key and adding it to the ssh-agent](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/generating-a-new-ssh-key-and-adding-it-to-the-ssh-agent)

---

## Open and start the project
The project is meant to run inside a container either as a devcontainer on your PC, a devcontainer on your Raspberry Pi or deployment container with topo.

### Open AMP with VS Code
Open the cloned repository folder in VS Code first.

![VS Code opened in the AMP repository](../../../static/img/04-starting-point-vscode.png)

* Open command palette:
  - Windows/Linux: Ctrl+Shift+P
  - macOS: Cmd+Shift+P
* Then select `Dev Containers: Reopen in Container`. A popup will appear.
   - For PC development choose "PC amp-dev-forge"
   - For Raspberry Pi on-device Hailo 8 / AI HAT+ development choose `RPI5 H8 amp-dev-forge`
   - For Raspberry Pi on-device Hailo 10 / AI HAT+ 2 development choose `RPI5 H10 amp-dev-forge`
* After a successful container build, every dependency, pre-commit hook, and device should be ready to use inside the Dev Container.

![VS Code command palette showing Reopen in Container](../../../static/img/05-reopen-in-container.png)

![VS Code Dev Container selection dialog](../../../static/img/06-reopen-in-container2.png)

Open a new terminal inside VS Code after the container is ready. The prompt should show that you are working inside the container workspace.

![VS Code terminal opened inside the Dev Container](../../../static/img/07-in-container-new-console.png)

On Raspberry Pi, `.devcontainer/platform_init.sh` runs on the host before container creation and generates the camera, audio, NPU, and shared-memory passthrough overrides for the selected service.

### Known Container issues
* If a required port is already reserved, the development or deployment container will not start.
* It is possible to use Docker only with Windows and WSL. In this case, Docker Desktop is not mandatory and host networking can also be used.

### Build AMP
- Open the Command Palette and run `Tasks: Run Task`, or use **Terminal -> Run Task...**
- **00 Build Project**: Builds all elements (default).
   - Before build, a popup should appear.
   - You will be prompted to choose `debug` or `release`. Use `debug` if unsure.
- **01 Clean Project**: Cleans build artifacts.
- **02 Build Tests**: Builds with tests enabled.
- **03 Run Tests**: Runs all tests.

![VS Code build task for AMP](../../../static/img/08-build-project.png)

### Start AMP

After a successful build, `amp-menu` will be created in the `tools` folder. This tool serves as the project entry point and simplifies GStreamer pipeline creation.

- Run the menu in a new terminal inside the container:
```bash
./tools/amp-menu
```
- Select a specific pipeline from the menu. Pipelines are defined under `config/pipelines`.
- To re-run the last-selected pipeline without the menu prompt:
```bash
./tools/amp-menu -l
```
At the moment, pipeline execution is fully synchronous end to end. An asynchronous inference execution flow is planned for a later update, but it is not available yet.

To stop an application that was not started from a VS Code launch configuration, press Ctrl+C in the console.

**First-time users:**  
- We recommend running **01-full-onnx** first.
It includes the main integrated ONNX pipelines and models currently available in the system.
- The shipped demo presets usually register their `ampinfer` elements with `active=false`.
  After the UI opens, use the **AI Models** panel to enable the models you want to run.

![AMP pipeline selection view](../../../static/img/09-select-pipeline.png)

To stop a pipeline:
- Windows/Linux: Ctrl + C  
- macOS: Control + C  

### Other available pipelines

Each pipeline's default source is an image, and the default sink is the `ampsink` endpoint. The pipeline files also contain premade alternative sources and sinks. Use them as templates when switching to a camera or video source.

- `01-full-onnx.json` — integrated ONNX model pipelines on a still image
- `02-full-onnx-hailo8.json` — integrated ONNX + Hailo 8 pipelines on a still image with ampsink video and optional audio sink
- `03-full-onnx-hailo8l.json` — integrated ONNX + Hailo 8L pipelines on a still image with ampsink video and optional audio sink
- `04-full-onnx-hailo10.json` — integrated ONNX + Hailo 10 pipelines on a still image with ampsink video and optional audio sink
- `cam-connect.json` — camera-contact demo
- `gaze-detection.json` — gaze-estimation demo
- `tracker-pc.json` — ONNX tracking demo
- `tracker-rpi.json` — Hailo 8 tracking demo

### Debug AMP
- Use the "AMP Debug latest" configuration in VS Code (F5). This will run the latest selected pipeline. Before debugging, a popup should appear. Select the release or debug target you want to use.
- Use the "AMP Debug selection" configuration in VS Code (F5). This will run the pipeline you select. Before debugging, a popup should appear. Select the release or debug target you want to use. Another popup will prompt you to select the specific pipeline you want to debug.

![VS Code Run and Debug view showing AMP Debug latest](../../../static/img/23-vscode-debug.png)

---

## Published Endpoints

Open a new terminal in the Dev Container to see the available endpoints. When in doubt, the following endpoints apply.

- Disclaimer: Microsoft Edge, Firefox, or Safari are the suggested browsers for the AMP web UI. If the image is not visible in the browser on Windows
   - Edge: open `edge://flags/`, find `#enable-webrtc-hide-local-ips-with-mdns`, and disable it.
   - Firefox: `about:config`, find `media.peerconnection.ice.obfuscate_host_addresses`, and disable it.

- [Raspberry AMP Web UI](http://raspberrypi.local:9999)
- [Raspberry AMP Documentation](http://raspberrypi.local:8080)
- [PC AMP Web UI](http://localhost:9999)
- [PC AMP Documentation](http://localhost:8080)

Once the UI is open, use the **AI Models** panel to enable the models you want to run and the **Controls** panel to toggle the performance overlay.

![AMP browser UI after opening the web view](../../../static/img/10-browser-ui.png)

- **Hostnames:**
   - `raspberrypi.local` (on Raspberry Pi)
   - `localhost` (on your development machine)
- **Ports:**
   - `9999` (AMP Web UI)
   - `8080` (Documentation)
   - **Besides these, the following ports are also used in the background: 8000, 8001**

---

## How to use a laptop's built-in webcam in WSL/Linux

 1. (Only for WSL users) Forward camera input to WSL.
    - Install [USBIPD](https://github.com/dorssel/usbipd-win/releases)
    - (Optional) Install [WSL USB Manager](https://github.com/nickbeth/wsl-usb-manager/releases) to get a GUI for USBIPD
    - Forward the camera to WSL by binding and attaching it with WSL USB Manager
        - Note: If attaching the camera fails, then disable the device in the Device Manager. Windows sometimes starts to use the camera in background processes and it is hard to figure out which process reserved it.
 2. Add the camera source to the pipeline and decode the stream before the models.
    - Eg.:

```json
"v4l2src device=/dev/video0 ! \"image/jpeg,width=1280,height=720,framerate=60/1\"  !",
"jpegdec !",
```

## Scripts and applications in our repository
Helper scripts can be found under the `scripts` folder. The root of that folder contains the scripts needed to build and run the project, while `scripts/private` contains helper scripts that are not normally used directly.

Important scripts for usage:
- `amp-menu`: Main launcher for pipelines and demos.
- `build-elements.sh`: Build all GStreamer elements.
- `docker-nuke.sh`: Stop and remove all Docker containers.
- `serve-docs.sh`: Serve docusaurus documentation.
- `serve-docs-plain.sh`: Serve plain HTML documentation locally from the Dev Container.
- `gen-doc.sh`: Generate documentation.


## Quality checks
`expkits-ci` is a tool that is installed automatically during container creation.
Most quality checks, both in CI and locally, are performed by this tool.

For help inside the container, run `expkits-ci --help`.
```bash
amp-dev-forge $ expkits-ci --help
usage: __main__.py [-h] [-bn] [-cm] [-jt] [-clfc] [-clf] [-clt] [-pyfc] [-pyf] [-cmfc] [-cmf] [-shfc] [-shf] [-lhc] [-lh] [-v] [-ac] [-do] [-pr PR_TARGET_BRANCH] [-lo {stdout,file,both}] [-lf LOG_FILE]
                  [-lof LIST_OF_FILES [LIST_OF_FILES ...]]

...

(.venv-ci) ubuntu@387b974701cb:/workspaces/amp-dev-forge$
```

To check your changes, a set of plugins is already configured in the environment, but you can also call `expkits-ci` directly or run the installed pre-commit hooks manually.

```bash
pre-commit run --all-files
```

> Note: `pre-commit run` only checks staged files by default. Use `--all-files` to check the entire working tree, or stage your changes first.

- To run without pre-commit hooks simply:

```bash
git commit --no-verify
```

---

## Next steps

If you want to move from using the project to extending it, continue to [Engineering starting point](engineering.md).

## What should you have at the end of this document?

By the end of this guide, you should have:

- a supported host setup with the main prerequisites installed
- working Git and SSH access for cloning the repository
- a working AMP Development Container or Topo deployment path
- a successful build of the runtime
- `amp-menu` running and at least one pipeline started
- access to the AMP UI and documentation endpoints

Success looks like this: you can build AMP, launch a pipeline, open the published UI in a browser, and continue into the engineering guides without guessing the next step.
