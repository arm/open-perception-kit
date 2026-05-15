# Arm Perception Kit

The Arm Perception Kit helps Raspberry Pi developers get from setup to useful
edge-vision inference without building the whole perception stack from scratch.

Use it when you want the fastest path from a ready Raspberry Pi to visible
inference and a starting point for a vision application. This developer preview
gives you a container-based workflow, a packaged pipeline, browser visibility,
model controls, performance and debug signals, and output paths you can adapt
instead of stitching those pieces together yourself.

You can run a reference pipeline on a Raspberry Pi 5, view the result in a
browser, and then adapt the source, model, pipeline, accelerator, or output path
for your own application.

## What you can do

- Run a reference vision pipeline on a Raspberry Pi.
- View the sample image stream, model controls, debug log, and performance
  signals in a browser.
- Enable bundled models and confirm that inference changes the viewer, debug
  log, or performance state.
- Use inference output as a starting point for downstream application logic.
- Adapt the source, pipeline, model, accelerator, or output path for a Raspberry
  Pi vision application.

## What to expect

Plan for around 30 minutes from opening a ready Raspberry Pi environment to
first inference. That assumes the prerequisites below are already in place and
that the Raspberry Pi can reach GitHub, package repositories, and any container
or source locations used during the first container build. Time to prepare the
Raspberry Pi OS image, SSH, Docker, Git, VS Code, or network access is outside
this guide. Camera input, Hailo acceleration, custom model work, and
application integration are follow-on paths.

This guide uses two machines:

- The host machine: your personal computer, used for VS Code, Remote SSH, and
  the browser viewer.
- The target Pi: the Raspberry Pi 5, where you clone the repository, open the
  devcontainer, build the kit, and run the reference pipeline.

The target Pi serves the WebRTC browser viewer on port `9999`. You open that
viewer from the host machine.

You need:

- Raspberry Pi 5 running the supported 64-bit Raspberry Pi OS image for this
  release. The current preflight expects a Debian Trixie-based image.
- SSH enabled on the Raspberry Pi, with a username and password or SSH key you
  can use from the host machine.
- Docker Engine and the Docker Compose plugin on the target Pi, usable by
  the account you use over SSH. The check below expects
  `docker info` to run without `sudo`.
- Git on the target Pi, with network access from the target Pi to the
  repository and build sources used by this release.
- VS Code, the Remote SSH extension, and the Dev Containers extension on your
  host machine.
- Network access from the host machine to the target Pi.
- Permission to run `sudo` on the target Pi.

## Quick install and first inference

Follow this path first. It gets the repository onto the Raspberry Pi, opens the
devcontainer, builds the kit, starts the reference pipeline, and opens the
browser viewer.

### Connect to the Raspberry Pi

Use the target Pi hostname or IP address from your prerequisite setup.

Run on the host machine:

```bash
ssh <raspberry-pi-username>@<raspberry-pi-hostname-or-ip>
```

**Expected result:** the host machine opens a shell on the target Pi.

### Check the starting state

Run on the target Pi:

```bash
cat /proc/device-tree/model
cat /etc/os-release
uname -m
groups
docker info
docker compose version
git --version
```

**Expected result:** the board is a Raspberry Pi 5, `/etc/os-release` reports the
release-supported Raspberry Pi OS image, `uname -m` reports an Arm 64-bit
architecture such as `aarch64`, `groups` includes `docker`, Docker is running,
Docker Compose is available, and Git is installed.

If one of these checks fails, the prerequisite setup is not complete. Resolve
the missing prerequisite before you continue.

### Get the repository on the Raspberry Pi

Run on the target Pi:

```bash
git clone https://github.com/Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

**Expected result:** the repository is at `~/amp-dev-forge` unless you chose another
folder. It contains `scripts/`, `.devcontainer/`, `config/`, `development/`,
and `tools/`.

If your environment requires SSH access to GitHub and GitHub SSH
authentication is already configured on the Raspberry Pi, use this clone
command instead:

Run on the target Pi:

```bash
git clone git@github.com:Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

### Run the readiness checks

Run on the target Pi from the repository folder. If you used the default
clone location, run:

```bash
cd ~/amp-dev-forge
scripts/pre-req.sh
```

If you cloned the repository somewhere else, go to that folder before running
`scripts/pre-req.sh`.

**Expected result:** the script confirms the Raspberry Pi model, OS baseline,
Docker, Git, and related repository checks, or names the exact item it could not
verify.

The current script is broader than the first still-image ONNX path. It also
checks GitHub SSH authentication, the `code` command, and Raspberry Pi packages
used by wider repository paths. Those checks are not blockers when you cloned
with HTTPS, run VS Code on the host machine, and the manual checks above
pass. Treat Raspberry Pi model, OS, Docker, Docker Compose, Git, and network
failures as blockers before opening the devcontainer.

### Open the devcontainer

On the host machine:

1. Open VS Code.
2. Open the command palette with **View > Command Palette**.
3. Run **Remote-SSH: Connect to Host**.
4. Enter `<raspberry-pi-username>@<raspberry-pi-hostname-or-ip>`.
5. Enter the target Pi account password or SSH key passphrase if VS Code asks
   for it. The prompt can appear in the command palette or near the top of the
   VS Code window.
6. If VS Code asks for the remote target platform, choose Linux.
7. Open the `amp-dev-forge` repository folder on the target Pi. If you used
   the default clone command, the folder is `~/amp-dev-forge`.
8. Run **Dev Containers: Reopen in Container**.
9. Select the Raspberry Pi 5 devcontainer if VS Code asks. In this repository
    snapshot, VS Code may show the first-run Raspberry Pi 5 container as
    **RPI5 H8 amp-dev-forge**. Use the Hailo 10 container only for a Hailo 10
    path.
10. Wait until VS Code reports that the container is ready. The first
    devcontainer build can take a few minutes while VS Code builds layers,
    installs extensions, and runs setup scripts.
11. Open a new terminal in VS Code.

Run inside the devcontainer:

```bash
pwd
ls /work/scripts/build-elements.sh /work/config/pipelines /work/config/models
```

**Expected result:** `pwd` prints `/work`, and the listed files and folders
exist. If the path is not `/work`, reconnect with Remote SSH, open
`~/amp-dev-forge` on the target Pi, and reopen it in the devcontainer.

### Build the kit

Run inside the devcontainer:

```bash
/work/scripts/build-elements.sh debug false
```

**Expected result:** the build completes without a blocking error and creates
`/work/tools/amp-menu`.

This command builds the Perception Kit elements in debug mode without building
tests.

The first build can take several minutes and may download packages, container
layers, or source dependencies. Leave the terminal running unless it prints a
blocking error.

### Run the reference pipeline

Run inside the devcontainer:

```bash
/work/tools/amp-menu 01-full-onnx
```

Keep this terminal running. The command starts the reference pipeline and runs
until you stop it with `Ctrl+C`.

`01-full-onnx` uses packaged still-image media, ONNX inference, the browser
viewer, and model controls. It does not require a camera or Hailo accelerator.

**Expected result:** the terminal prints the generated `gst-launch-1.0` command and does
not exit with a missing-plugin, missing-model, or missing-device error.

### Open the viewer

On the host machine, open:

```text
http://<raspberry-pi-ip-address>:9999
```

You can use the hostname instead if it resolves reliably on your network:
`http://<raspberry-pi-hostname>:9999`.

**Expected result:** the viewer loads and shows the sample image stream,
connection status, controls, the **AI Models** panel, and the debug log. The
first source is a still image repeated as video, so it is normal if the picture
itself does not move.

### Enable a model and confirm inference

In the WebRTC browser viewer, use the model toggles on the right-hand side to
enable **Ultraface**. The reference pipeline uses
`/work/data/images/katana.jpg`, which contains a visible face, so Ultraface is
the clearest first model check.

For the first run, enable **Ultraface** only. Ignore the other model toggles
until the first inference path works. Some listed models need different source
media or output from another model.

![WebRTC viewer showing inference on a sample image with model toggles on the right-hand side](assets/img/browser-viewer-content.png)

You have completed the first run when:

- The pipeline keeps running.
- The viewer shows the sample image stream.
- The **AI Models** panel lists models.
- **Ultraface** can be enabled.
- Enabling **Ultraface** produces the expected first signal: a face-detection
  marker appears on the sample image. If the marker does not render, use the
  debug log, model state, or performance overlay as fallback evidence that
  Ultraface became active.

Use [Bundled models and pipelines](reference/bundled-models-and-pipelines.md)
when you want to try another model deliberately.

## If the first run fails

| Symptom | Do this first |
| --- | --- |
| VS Code opens a local folder | Reconnect with Remote SSH and open the repository folder on the Raspberry Pi. |
| Devcontainer terminal is not in `/work` | Reopen the Raspberry Pi repository in the devcontainer. |
| `docker compose` is missing | The Raspberry Pi prerequisite setup is incomplete; resolve Docker Compose before reopening the devcontainer. |
| Readiness script reports only GitHub SSH, `code`, or broader package checks | Continue if you cloned with HTTPS, use VS Code on the host machine, and the manual target Pi checks pass. |
| Readiness script exits with `get_device_model` | Use the manual Raspberry Pi checks above and report the script failure; do not ignore model, OS, Docker, Compose, Git, or network failures. |
| Build fails before `amp-menu` exists | Fix the first missing package, permission, or container error shown in the build output. |
| Pipeline exits immediately | Run `/work/tools/amp-menu -p 01-full-onnx` and inspect the first missing plugin, model, or file in the generated command. |
| Viewer does not load | Keep the pipeline terminal running, use the target Pi IP address, and check that port `9999` is reachable from the host machine. |
| Viewer loads but stream or controls do not connect | Keep the pipeline running and check whether your network allows browser access to the Raspberry Pi ports used by the viewer and its connections: `9999`, `8000`, and `8001`. |
| Models appear but output does not change | Confirm the model is enabled and that the sample media is expected to produce output for that model. |

For more symptoms, use [Troubleshooting](troubleshooting/index.md).

## Important limitations

- This developer preview is for evaluation, early application development, and
  feedback.
- The first run proves one reference vision path. It does not prove every
  checked-in pipeline, model, source, sink, or hardware combination.
- Camera input, Hailo acceleration, custom models, tracking, and application
  output are useful follow-on paths. Add one variable at a time.
- Hailo acceleration requires matching hardware, Raspberry Pi Hailo software,
  device access, presets, and compiled model artifacts.

Use [Developer preview scope](start-here/support-matrix.md) for the full support
matrix.

## After first success

Pick the next step that moves your application forward. Change one variable at
a time so you know what caused the result.

| Goal | Good next step | Why start there |
| --- | --- | --- |
| Use your own input or output path | [Change a source or sink](how-to/change-source-sink.md) | This is the closest step from first inference: keep the known pipeline working while you replace packaged media or change where output goes. |
| Use live camera input | [Run camera inference](tutorials/run-camera-inference.md) | Move from packaged media to a USB or Raspberry Pi camera after the first pipeline works. |
| Feed inference into an application | [Use output in an app](how-to/use-output-in-app.md) | Learn how to capture inference output so downstream application logic can use it. |
| Add or adapt a model | [Add a model and OpChain](how-to/add-model-opchain.md) | Change the model once the source, sink, and output path are stable enough to isolate model issues. |
| Use Hailo acceleration | [Run Hailo inference](tutorials/run-hailo-inference.md) | Add accelerator hardware after the ONNX path works on the Raspberry Pi. |
| Understand the pieces | [How the kit works](start-here/how-the-kit-works.md) | Read this when you want the conceptual map of the Raspberry Pi, devcontainer, pipeline, model, viewer, and output pieces. |
