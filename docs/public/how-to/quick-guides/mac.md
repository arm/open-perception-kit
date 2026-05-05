---
sidebar_position: 2
sidebar_label: macOS Quick Guide
---

# macOS Quick Guide

This is the shortest path from cloning the repository to running the first AMP pipeline on macOS.

Use this page if you want the quickest first run.
Use the other how-to pages if you want setup details, troubleshooting help, or deeper explanations.

## What will you learn from this documentation?

If you follow this page successfully, you will learn how to:

- prepare a supported macOS host for AMP development
- open the repository in the expected container workflow
- build the project and start `amp-menu`
- run the first pipeline and verify that the local web UI is reachable

At the end of this guide, you should have AMP running on your macOS host, with the first pipeline launched and the web UI available at `http://localhost:9999`.

## 1. Install the host tools

The following section should detail the platform specific prerequisites that this document assumes are already met: [Deep dive prerequisites section](../deep-dives/index.md#prerequisites)

Although not complete, a check script can help the user determine whether the prerequisites are met: "./scripts/pre-req.sh"

Install:
- Git
- Docker Desktop (for Colima see [Colima Mac Installation](colima-mac.md))
- Visual Studio Code
- VS Code Dev Containers extension

> Colima is not the recommended path here.
> The existing project docs treat Docker Desktop as the expected setup.

## 2. Make sure your Git SSH key is ready

If you clone with SSH, start `ssh-agent` and add your key first.

```bash
eval "$(ssh-agent -s)"
ssh-add <your-private-key>
```

> Expected result: Git operations over SSH should work without prompting for a password on every repository access.

For key creation details, see [Generating a new SSH key and adding it to the ssh-agent](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/generating-a-new-ssh-key-and-adding-it-to-the-ssh-agent).

## 3. Clone the repository

In order to download the latest release archive download the compressed source package from the [release page](https://github.com/Arm-Debug/amp-dev-forge/releases) and extract it before continuing.
![GitHub release page showing the source code download](../../../static/img/01-repo-compressed.png)

Extract the ZIP archive:

```bash
unzip amp-dev-forge-${VERSION}.zip
mv amp-dev-forge-${VERSION} amp-dev-forge
cd amp-dev-forge
```

Extract the tar archive:

```bash
tar -xzf amp-dev-forge-${VERSION}.tar.gz
mv amp-dev-forge-${VERSION} amp-dev-forge
cd amp-dev-forge
```

If you use the archive path, continue from the next step after `cd amp-dev-forge`.

Alternatively you can clone the repository with Git.

![AMP Development Forge repository root](../../../static/img/02-repo-root.png)

```bash
git clone git@github.com:Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

![Terminal output after cloning the repository](../../../static/img/03-repo-clone.png)

> Expected result: the `amp-dev-forge` folder exists locally and VS Code can open it.

## 4. Open the repository in VS Code

Open the cloned folder in VS Code. Use **File -> Open Folder...**, or run the following command from the repository root if `code` is available in your shell path:

```bash
code .
```

![VS Code opened in the AMP repository](../../../static/img/04-starting-point-vscode.png)

Then:
- open the Command Palette with `Cmd+Shift+P`
- run `Dev Containers: Reopen in Container`
- choose `PC amp-dev-forge`

![VS Code command palette showing Reopen in Container](../../../static/img/05-reopen-in-container.png)

![VS Code container selection dialog](../../../static/img/06-reopen-in-container2.png)

Wait until the host side container finishes building.

Open a new terminal inside VS Code after the container is ready. The prompt should show that you are working inside the container workspace.

![VS Code terminal opened inside the host side container](../../../static/img/07-in-container-new-console.png)

> Expected result: VS Code reconnects into the host side container and the project opens with the container environment active.

## 5. Build the project

Use the build task in VS Code:
- open the Command Palette and run `Tasks: Run Task`
- run **00 Build Project**

![VS Code build task for AMP](../../../static/img/08-build-project.png)

Or build in the active host side container terminal:

```bash
./scripts/build-elements.sh debug false
```

> Expected result: the build completes successfully and `tools/amp-menu` is available.

## 6. Start AMP

Use the VS Code run task:

- open the Command Palette and run `Tasks: Run Task`
- run **00 Run project and select pipeline**
- choose `01-full-onnx`

The menu view is also available through **00 Run project with menu**.

You can also run the menu in a new terminal inside the active host side container from the project root:

```bash
./tools/amp-menu
```

The menu should show the available pipeline presets.

![AMP pipeline selection view](../../../static/img/09-select-pipeline.png)

Stop:

To stop an application that was not started from a VS Code launch configuration, press Control+C in the console.

> Expected result: the selected task or `amp-menu` starts and either launches the selected pipeline or shows the pipeline selection menu.

> Disclaimer. During GStreamer pipeline runs, some errors caused by browser connection issues or dropped frames are expected. These can be ignored; a more verbose logging system is in progress.

## 7. Run the example pipeline

For the shortest first run, choose `01-full-onnx`.
If you opened the interactive menu, type the corresponding number and press Enter.

This is the shortest recommended first pipeline.

> Expected result: the selected pipeline launches and the web UI can later list the preset's models.

## 8. Open the web UI

Safari is the suggested browser for the AMP web UI on macOS. If the image is not visible or unstable, see [Troubleshooting: Browser and WebRTC connection issues](../deep-dives/troubleshooting.md#browser-and-webrtc-connection-issues).

Open:
- http://localhost:9999

Documentation is available at:
- http://localhost:8080

In the **AI Models** panel, enable one or more models to start inference.
The main demo presets register their models as inactive by default so you can switch them on individually.

![AMP browser UI after opening the web view](../../../static/img/10-browser-ui.png)

Use the model controls to enable or disable selected models. The demo presets usually start with models disabled, so this is the normal way to begin inference after the page opens.

![AMP browser UI model enable and disable controls](../../../static/img/24-browser-ui-enable-disable.png)

The performance overlay is available after at least one model is enabled.

![AMP browser UI with the performance overlay visible](../../../static/img/24-browser-ui-performance-overlay1.png)

Use the performance overlay button to show or hide the performance data.

![AMP browser UI performance overlay toggle button](../../../static/img/24-browser-ui-performance-overlay2.png)

The log window shows browser-side connection messages and RTC connection debug data. Use it when the web UI opens but the video connection is unstable or does not appear.

![AMP browser UI log window with RTC connection messages](../../../static/img/24-browser-ui-logwindow.png)

> Expected result: the AMP UI opens in your browser, the documentation endpoint is reachable, and enabled models begin producing overlays or results.

## 9. Run it again later without the menu

After you have selected a pipeline once, you can rerun the last selection with the -l (latest) argument:

The VS Code task for this is **00 Run project with latest pipeline**.

```bash
./tools/amp-menu -l
```

> Expected result: AMP starts the most recently selected pipeline directly without showing the menu.

## If you want the deeper guides

Continue with the [deep dive how-to guide](../deep-dives/index.md).

If you want a guided repository walk-through, continue with the [exercise quick guide](exercise.md).

## What should you have at the end of this document?

By the end of this guide, you should have:

- a working host side container for AMP on a macOS host
- a successful local build
- `amp-menu` starting correctly from the VS Code task or the active host side container terminal
- `01-full-onnx.json` running at least once
- the AMP web UI reachable at `http://localhost:9999`

Success looks like this: the container opens correctly, the build completes, the pipeline starts from the VS Code task or `amp-menu`, and the browser can reach the AMP UI.
