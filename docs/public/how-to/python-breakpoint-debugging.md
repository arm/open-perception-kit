---
title: Debug Python Scripts with Breakpoints
sidebar_position: 10
sidebar_label: Python breakpoints
description: Attach debugpy to an OPK pipeline, set a breakpoint in a PythonScript callback, and inspect inference tensors.
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Debug Python Scripts with Breakpoints

Attach `debugpy` to the GStreamer process running an
`opk-python-ops/PythonScript` callback. Then set a breakpoint in the editor and
inspect the inference outputs. The callback stays unchanged.

Start with the OPK video pipeline you created and attach after it starts
processing frames. The [BlazeFace example](../../../development/examples/byom-blazeface/README.md)
illustrates the steps. The debugger was verified in an x86-64 OPK development
container with CPython 3.13 and `debugpy` 1.8.22. The editor steps use VS Code with the
**Dev Containers** and **Python Debugger** extensions. Other compatible Debug
Adapter Protocol (DAP) clients can connect to the same debug server.

If your pipeline is already processing frames in your development container, go
to [step 4](#4-install-the-debugger-tool) and use the existing pipeline's PID.

## 1. Build and enter the development container

On the host, open a terminal in the repository root. Build and start the
development container, then enter its shell.

```bash
./scripts/quick_start.sh
./scripts/enter_cli.sh
```

Run the remaining shell commands **inside this container** from `/work`, the
mounted checkout.

## 2. Build OPK with Python Ops enabled

The container supplies CPython and the locked Python dependencies. Set
`OPK_PYTHON_OPS=enabled` when building OPK to compile `opk-python-ops.so`, the
native module that runs Python callbacks. This build setting maps to Meson's
`-Dpython_ops=enabled`. The `debug` argument selects the native build mode.

```bash
cd /work
OPK_PYTHON_OPS=enabled ./scripts/build.sh debug
ls -l development/build-active/meson-out/opk-python-ops.so
```

## 3. Prepare your model and check the callback

Check that the model and media files referenced by your pipeline are available
inside the container. If you are following the BlazeFace example, use the
repository downloader to provision its public model. The descriptor pins the
model revision and checksum.

```bash
python3 scripts/download-models.py \
  --models-dir development/examples/byom-blazeface
ls -l development/examples/byom-blazeface/face_detector.onnx
```

For BlazeFace, run the OpChain once on the JPEG before starting the debugger.

```bash
./tools/opchain-exec \
  development/examples/byom-blazeface/opchain.json \
  data/images/GettyImages-1129703310-thumbnail.jpg
```

The runner prints `Perception packet bytes` after successful execution. It may
also print `Unknown payload type` for BlazeFace's caller-owned JSON payload.
The [example's result consumer](../../../development/examples/byom-blazeface/support/results.py)
knows how to decode it. Fix any setup or callback errors before proceeding.

## 4. Install the debugger tool

Linux PID attachment uses GDB. Use the container's `uv` to install the tested
`debugpy` version in its own tool environment. Use the absolute executable path
from `uv tool dir --bin` so the same installation is available for the `sudo`
retry if needed.

```bash
command -v gdb
uv tool install --python "$OPK_PYTHON_RUNTIME_VENV/bin/python" 'debugpy==1.8.22'
debugpy_bin="$(uv tool dir --bin)/debugpy"
"$debugpy_bin" --version
```

The last command should print `1.8.22`. See the [uv tool documentation](https://docs.astral.sh/uv/concepts/tools/).

## 5. Start your pipeline and find its PID

If your pipeline is already processing frames, leave it running and go straight
to the PID check below. Otherwise, start it with your usual launcher. For a
saved pipeline preset, replace the path below with the path to the JSON file
you created. Run it in the foreground from `/work` with the same environment
variables you use for normal playback.

```bash
./tools/opk-menu /work/config/pipelines/my-pipeline.json
```

If you are following the BlazeFace example, its normal runner starts the
example's existing preset. Confirm the prerecorded video is present, then run
the example.

```bash
ls -l /work/data/videos/GettyImages-1129703310.mov
cd /work/development/examples/byom-blazeface
python3 run.py
```

If your video ends too quickly to attach, set `"loop": true` in your existing
preset, increment its patch version, and restart. For BlazeFace, edit
`development/examples/byom-blazeface/pipeline.json`. Playback must keep
delivering frames to the Python callback until the debugger connects. See
[how OPK loops video presets](../concepts/runtime-basics.md#what-is-a-pipeline).

Leave playback running. Open a second terminal inside the same container to
find the process. Identify the line for your preset or pipeline. If it is
missing, check the launcher terminal for a startup error or end of playback.

```bash
pgrep -af 'opk-menu|gst-launch-1\.0'
```

Set `opk_debug_pid` to the number at the start of the matching line in this
second terminal. Replace `12345` with the actual PID and confirm it is still
running.

```bash
opk_debug_pid=12345
ps -p "$opk_debug_pid" -o pid=,args=
```

## 6. Attach debugpy to the running pipeline

Run the attach command **inside the same container**, after the pipeline has
started processing frames. Keep the listener on loopback. If you started with
`opk-menu` or the BlazeFace runner, use the PID of `opk-menu`. It runs
GStreamer in-process and prints a `gst-launch-1.0` command for diagnostics.
For a pipeline launched directly
with `gst-launch-1.0`, use that process's PID.
See the [debugpy PID attachment reference](https://github.com/microsoft/debugpy/wiki/Command-Line-Reference#--pid-pid).

```bash
debugpy_bin="$(uv tool dir --bin)/debugpy"
"$debugpy_bin" --listen 127.0.0.1:5678 --pid "${opk_debug_pid:?Set the pipeline PID first}"
```

If GDB reports `ptrace: Operation not permitted` in the standard OPK
development container, retry with the container's `sudo` access. The
[Compose configuration](../../../compose.base.yaml) grants `SYS_PTRACE` to
the container, while the `dev` shell may lack that effective capability.
Run this only from the development container where you started the pipeline.
See the [Linux ptrace policy](https://docs.kernel.org/admin-guide/LSM/Yama.html#ptrace-scope).

```bash
sudo "$debugpy_bin" --listen 127.0.0.1:5678 --pid "${opk_debug_pid:?Set the pipeline PID first}"
```

After a successful injection, GDB detaches and the Python debug server stays
running. Attaching GDB can briefly interrupt playback. Confirm the listener
before opening the editor debugger.

```bash
ss -ltn 'sport = :5678'
```

Look for a `LISTEN` row with local address `127.0.0.1:5678`.

## 7. Connect VS Code and set a breakpoint

1. In VS Code connected to the Docker host, open the Command Palette and choose
   **Dev Containers: Attach to Running Container…**. Select the running OPK
   development container. See the [VS Code container attach guide](https://code.visualstudio.com/docs/devcontainers/attach-container).
2. In that container window, open `/work` as the workspace folder. Enable the
   **Python Debugger** extension in the container.
3. Open the Python script configured in your OpChain. Click the gutter next
   to a line inside `process()` to set a breakpoint. For BlazeFace, open
   `development/examples/byom-blazeface/postprocess.py` and choose the line
   `boxes, scores = _validated_outputs(tensors)`.
4. Open the Command Palette, choose **Debug: Select and Start Debugging**, then
   choose **Attach to PyBind**. The repository's
   [launch configuration](../../../.vscode/launch.json) connects to port `5678`.
   For another DAP client, select Python attach and connect to `127.0.0.1:5678`
   from the same container.
5. Wait for the editor to highlight the breakpoint line and show the `process()`
   frame in **Call Stack**. The next frame that calls the script reaches the
   breakpoint. With a video sink, the picture stays on the last displayed frame
   while the callback is paused. The pipeline stays in `PLAYING` while it waits
   for the callback to return.

Choose the attach configuration in step 4. Running `postprocess.py` directly
with **Run Python File** or **Python Debugger: Current File** raises an import
error because the standalone interpreter lacks OPK's native bridge modules.

## 8. Inspect tensors and step through the callback

Select the `process()` frame in **Call Stack**. Enter these expressions in the
**Debug Console** one at a time to inspect output names, shapes, dtypes, sample
values, and quantization metadata.

```python
[(t.index, t.name, t.array.shape, str(t.array.dtype)) for t in tensors]
tensors[0].array.reshape(-1)[:12]
(tensors[0].quantized, tensors[0].scale, tensors[0].zero_point)
```

Use **F11** to step into a function such as BlazeFace's `_validated_outputs`
and **F10** to step over the current line. To let playback continue, disable
the breakpoint and press **F5**. With the breakpoint enabled, the callback can
pause again on the next frame. See
[Python debugging in VS Code](https://code.visualstudio.com/docs/python/debugging).

Tensor arrays are read-only views valid during the callback. Make an explicit
`.copy()` to retain values beyond that call. A paused callback blocks the
calling pipeline thread. See the [Python Script Op tensor contract](python-script-op.md#tensor-contract).

## 9. End the debug session

Resume the callback and disconnect the editor debugger. When you are finished
with the pipeline, stop it using its normal controls. For a pipeline started
in the terminal in step 5, press **Ctrl+C** in the launcher terminal.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| `invalid --pid` with `invalid literal for int() ... ''` | The PID variable is empty. Recover the running pipeline PID with `pgrep` in step 5. |
| `ptrace: Operation not permitted` | Use the `sudo` retry in step 6 in the standard OPK development container. If it is still denied, the environment needs to allow debugger attachment. |
| The editor cannot connect | Check for `LISTEN` on port `5678` inside the same container and select **Attach to PyBind**. Check whether another client is already attached. |
| `No module named 'open_perception_kit_bridge'` | Stop the standalone Python session. Open `/work` in the container window and start **Attach to PyBind**. |
| The debugger connects but the breakpoint never hits | Check that frames are reaching `opkinfer` and the OpChain loads the same file you opened in the editor. If the path matches, check [debugpy tracing on native threads](https://github.com/microsoft/debugpy/wiki/API-Reference#debug_this_thread). |

Use the [native debugger configurations](../../../.vscode/launch.json) to step
through C++ preprocessing. The Python callback exposes inference outputs.

[Back to How-To Guides](/how-to)
