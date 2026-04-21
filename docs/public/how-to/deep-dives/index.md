---
sidebar_position: 3
sidebar_label: How-To
---

# AMP Development Forge How-To

This is the deep-dive setup and usage guide.

If you only want the shortest path to a first run, use the platform quick-guide pages instead.
If you want the fuller setup path and the next documentation hub finishing this tutorial, continue from here to [Engineering starting point](engineering.md).

## What will you learn from this documentation?

If you follow this guide successfully, you will learn how to:

- prepare a supported host or Raspberry Pi target for AMP
- clone the repository and open it in the expected container workflow
- build the project, run `amp-menu`, and start a first pipeline
- find the published endpoints and continue into the next engineering-focused documents

At the end of this guide, you should have a working AMP environment on your desk, a first pipeline running, and a clear path to the next deep-dive topics.

## Quick Overview
- **Goal:** Get AMP running locally or on a Raspberry Pi target
- **You'll need:** Docker, VS Code, Git, and an SSH key
- **Recommended first run:** ONNX pipeline

**Steps:**
1) [Clone the amp repository](#clone-the-repository)
2) [Install dependencies](#host-side-dependencies)
3) [Open and build the project](#open-and-start-the-project) or [deploy with Topo](topo.md)

---

### Clone the repository
For PC development, clone the repository on your host.
For on-device Raspberry Pi 5 development, clone the repository after setting up SSH successfully.
Alternatively you can develop on your PC and deploy to the target with Topo.

```bash
git clone git@github.com:Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

## Host side dependencies

We currently support four targets: Windows with WSL, Linux, macOS, and Raspberry Pi 5.
Install the required tools on your host for the target you plan to use.

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

### Raspberry Pi 5
   * **Docker**
   * **Hailo packages**
   * **v4l-utils**
   * **raspicam**
   * [Required device and required packages on the target](rpi5.md)
   * [Setup SSH connection](#ssh-setup)

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

## Open and Start the project
The project is meant to run inside a container either as a devcontainer on your PC, a devcontainer on your Raspberry Pi or deployment container with topo.

### Open AMP with VS Code
* Open command palette:
  - Windows/Linux: Ctrl+Shift+P
  - macOS: Cmd+Shift+P
* Then select "Reopen in Container". A popup will appear.
   - For PC development choose "PC amp-dev-forge"
   - For Raspberry Pi on device development choose "RPI5 amp-dev-forge"
* After a successful container build, every dependency, pre-commit hook, and device should be ready to use inside the Dev Container.

### Known Container issues
* If a required port is already reserved, the development or deployment container will not start.
* It is possible to use Docker only with Windows and WSL. In this case, Docker Desktop is not mandatory and host networking can also be used.

### Build AMP
- **00 Build Project**: Builds all elements (default).
   - Before build, a popup should appear.
   - You will be prompted to choose `debug` or `release`. Use `debug` if unsure.
- **01 Clean Project**: Cleans build artifacts.
- **02 Build Tests**: Builds with tests enabled.
- **03 Run Tests**: Runs all tests.

### Start AMP

After a successful build, `amp-menu` will be created in the `tools` folder. This tool serves as the project entry point and simplifies GStreamer pipeline creation.

- Run the menu:
```bash
./tools/amp-menu
```
- Select a specific pipeline from the menu. Pipelines are defined under `config/pipelines`.
- To re-run the last-selected pipeline without the menu prompt:
```bash
./tools/amp-menu -l
```
At the moment, pipeline execution is fully synchronous end to end. An asynchronous execution flow is planned for a later update, but it is not available yet.

To stop an application that was not started from a VS Code launch configuration, press Ctrl+C in the console.

**First-time users:**  
- We recommend running **01-full-onnx** first.
It includes the main integrated ONNX pipelines and models currently available in the system.

To stop a pipeline:
- Windows/Linux: Ctrl + C  
- macOS: Control + C  

### Other available pipelines

Each pipeline's default source is an image, and the default sink is the `ampsink` endpoint. The pipeline files also contain premade alternative sources and sinks. Use them as templates when switching to a camera or video source.

- `01-full-onnx.json` — integrated ONNX model pipelines on a still image
- `02-full-onnx-hailo.json` — integrated ONNX + Hailo pipelines on camera and audio input
- `cam-connect.json` — camera-contact demo
- `gaze-detection.json` — gaze-estimation demo
- `tracker-pc.json` — ONNX tracking demo
- `tracker-rpi.json` — Hailo tracking demo

### Debug AMP
- Use the "AMP Debug latest" configuration in VS Code (F5). This will run the latest selected pipeline. Before debugging, a popup should appear. Select the release or debug target you want to use.
- Use the "AMP Debug selection" configuration in VS Code (F5). This will run the pipeline you select. Before debugging, a popup should appear. Select the release or debug target you want to use. Another popup will prompt you to select the specific pipeline you want to debug.

---

## Published Endpoints

Open a new terminal in the Dev Container to see the available endpoints. When in doubt, the following endpoints apply.

> Disclaimer: Microsoft Edge is the suggested browser for the AMP web UI. If the image is not visible in the browser, open `edge://flags/`, find `#enable-webrtc-hide-local-ips-with-mdns`, and disable it.

- [Raspberry AMP Web UI](http://raspberrypi.local:9999)
- [Raspberry AMP Documentation](http://raspberrypi.local:8080)
- [PC AMP Web UI](http://localhost:9999)
- [PC AMP Documentation](http://localhost:8080)

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
- `serve-docs-plain.sh`: Serve plain HTML documentation locally from devcontainer.
- `gen-doc.sh`: Generate documentation.

---

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
In order to not just use the project but extend it with your own cool stuff continue to [Engineering starting point](engineering.md).

## What should you have at the end of this document?

By the end of this guide, you should have:

- a supported host or Raspberry Pi setup with the main prerequisites installed
- working Git and SSH access for cloning the repository
- a working AMP Dev Container or Topo deployment path
- a successful build of the runtime
- `amp-menu` running and at least one pipeline started
- access to the AMP UI and documentation endpoints

Success looks like this: you can build AMP, launch a pipeline, open the published UI in a browser, and continue into the engineering guides without guessing the next step.
