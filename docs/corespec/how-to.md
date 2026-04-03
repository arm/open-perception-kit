# AMP Development Forge How-To

## Quick Overview
- **Goal:** Get AMP running locally or on a Raspberry Pi target
- **You'll need:** Docker, VSCode, Git, and an SSH key
- **Recommended first run:** ONNX pipeline

**Steps:**
1. Install dependencies
2. Configure SSH
3. Clone the repository
4. Open in DevContainer
5. Build and run AMP

---

## Host side dependencies
### Windows
   * [WSL](https://learn.microsoft.com/en-us/windows/wsl/install)
   * [Git](https://git-scm.com/install/)
   * [Docker Desktop](https://www.docker.com/products/docker-desktop/)
   * [Visual Studio Code](https://code.visualstudio.com/download)
   * **VSCode Dev Containers extension**
   * **WSL USB Manager 5.7.0** (Windows WSL)

### Linux
   * **Git**
   * **Docker**
   * **Visual Studio Code**
   * **VSCode Dev Containers extension**
   * **video4l2**

```bash
sudo apt-get update
sudo apt-get install -y git docker.io code v4l-utils
```

### Mac
   * **Git**
   * **Docker (Desktop)**
   * **Visual Studio Code**
   * **VSCode Dev Containers extension**

### Raspberry target
   * **Docker**
   * **Hailo packages**
   * **video4l2**
   * **raspicam**
   * **Minimum 8GB RAM (16GB recommended)**
   * For further details on Raspberry PI5 host installations please check out the relevant page: [How-To RPI5](how-to-rpi5.md)
   * If you want to clone AMP and deploy it straight to a remote target with Topo, see [How-To Topo](how-to-topo.md).

```bash
sudo apt-get update
sudo apt-get install -y git docker.io v4l-utils libraspberrypi-bin
```

> Note: These instructions are validated for Raspberry Pi 5. Earlier Raspberry Pi versions may require different packages or may not be fully supported.

---

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

## Start the project
### Clone the repository
Either on your host or in case of Raspberry PI5 development open the repository with [Remote development extension](https://code.visualstudio.com/docs/remote/ssh).
For this to work you must be on the same local network as your raspberry device

```bash
git clone git@github.com:Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

### Open AMP with VSCode
* Open command palette:
  - Windows/Linux: Ctrl+Shift+P
  - macOS: Cmd+Shift+P
* Then "Reopen in Container"
* At this point every dependency, pre commit hook, and device should be ready to use inside the devcontainer.

### Build AMP
- **00 Build Project**: Builds all elements (default).
  - You will be prompted to choose Debug or Release. Use Debug if unsure.
- **01 Clean Project**: Cleans build artifacts.
- **02 Build Tests**: Builds with tests enabled.
- **03 Run Tests**: Runs all tests.

### Start AMP
- Run the menu:
```bash
./scripts/amp-menu
```
- Select a specific pipeline from the menu (pipelines are defined under `scripts/pipelines`).
- To re-run the last-selected pipeline without the menu prompt:
```bash
./scripts/amp-menu -l
```

**First-time users:**  
- Recommended to run the **ONNX pipeline**

To stop a pipeline:
- Windows/Linux: Ctrl + C  
- macOS: Control + C  

### Debug AMP
- Use the "AMP Debug" configuration in VSCode (F5).

---

## Published Endpoints

- [Raspberry AMP Web UI](raspberrypi.local:9999)
- [Raspberry AMP Documentation](raspberrypi.local:8080)
- [PC AMP Web UI](localhost:9999)
- [PC AMP Documentation](localhost:8080)

- **Hostnames:**
   - `raspberrypi.local` (on Raspberry Pi)
   - `localhost` (on your development machine)
- **Ports:**
   - `9999` (AMP Web UI)
   - `8080` (Documentation)
   - Other ports may be used by ampsink or for streaming endpoints.

---

## Scripts and applications in our repository
Important scripts for usage:
- `amp-menu`: Main launcher for pipelines and demos.
- `build-elements.sh`: Build all GStreamer elements.
- `dev_init.sh`: Initialize dev environment and generate device YAMLs.
- `gen_audio.sh`, `gen_cam.sh`, `gen_npu.sh`, `gen_shared_memory.sh`: Generate docker-compose overrides for audio, camera, NPU, and shared memory.
- `docker-nuke.sh`: Stop and remove all Docker containers.
- `deployment-process.sh`: Steps for deployment.
- `serve-docs.sh`: Serve documentation locally.
- `run-ampperformance.sh`: Run performance overlay demo.
- `run-console`: Start a console in the devcontainer.
- `gen-doc.sh`: Generate documentation.

---

##  Quality checks
   expkits-ci is a tool that is automatically installed during the creation of the container.
   Most quality checks, both in CI and locally, are performed by this tool.

   For help on the `container side`, run: `expkits-ci --help`
```bash
amp-dev-forge $ expkits-ci --help
usage: __main__.py [-h] [-bn] [-cm] [-jt] [-clfc] [-clf] [-clt] [-pyfc] [-pyf] [-cmfc] [-cmf] [-shfc] [-shf] [-lhc] [-lh] [-v] [-ac] [-do] [-pr PR_TARGET_BRANCH] [-lo {stdout,file,both}] [-lf LOG_FILE]
                  [-lof LIST_OF_FILES [LIST_OF_FILES ...]]

...

(.venv-ci) ubuntu@387b974701cb:/workspaces/amp-dev-forge$
```

   To check your changes, a set of plugins are already set up in the environment, but you can alternatively:
- Call expkits-ci directly.
- Run the following task: 9 - Run CI checks for current file.
- Run the following task: 9 - Run full CI checks.
- Run the installed pre-commit hooks manually or with a commit.

```bash
pre-commit run
```

- To run without pre commit hooks simply:

```bash
git commit --no-verify
```

---

##  How to use (laptop's built-in) webcam in WSL/Linux

 1. (Only for WSL users) Forward camera input to WSL
    - Install [USBIPD](https://github.com/dorssel/usbipd-win/releases)
    - (Optional) Install [WSL USB Manager](https://github.com/nickbeth/wsl-usb-manager/releases) to have a GUI for USBIPD
    - Forward camera image to WSL by "Binding" and "Attaching" the camera with the WSL USB Manager
        - Note: If attaching the camera fails, then disable the device in the Device Manager. Windows sometimes starts to use the camera in background processes and it is hard to figure out which process reserved it.
 2. Add the camera resource to the pipeline and decode the stream before the models
    - Eg.:
      ```json
      "v4l2src device=/dev/video0 ! \"image/jpeg,width=1280,height=720,framerate=60/1\"  !",
      "jpegdec !",
      ```
