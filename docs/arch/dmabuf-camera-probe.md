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

# GStreamer DMA buffer camera probe

EXPKITS-1471 tests camera DMA buffer capture and display using GStreamer alone.
The Integrated Camera now produces a complete raw image through `glimagesink`'s
`DirectDmabufExternal` importer, confirmed visually. The raw pipeline has no OPK
elements, JPEG decoder, or `videoconvert`. GL uses software rendering on this
host, so this does not prove hardware acceleration or the absence of CPU copies.

The initial green image came from incomplete USB capture. Initializing the
Windows USB driver's speed state fixed the main failure. Occasional damaged
frames still occur; the script drops them, which can cause brief pauses.

## Select the integrated camera

Run on the WSL host with GStreamer's `v4l2src`, `glimagesink`, and `jpegdec`
plugins installed and the integrated camera attached through USB/IP:

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --list-formats-ext
```

In the tested attachment, `/dev/video0` is the RGB capture node, supporting
MJPG and YUYV; `/dev/video2` is the grayscale IR capture node. Both report the
same camera name. The `Integrated_RGB_Camera` symlink under `/dev/v4l/by-id`
points to the IR node on this setup, so verify the formats when selecting a node.
Node numbers may change after attachment.

The user needs membership in the Linux `video` group. After adding membership,
use a new login shell or run the commands below with `sg video -c 'COMMAND'`
from an existing shell. Preserve the WSLg display environment.

## Initialize the Windows USB driver's speed state

After attaching the camera, run from the WSL repository root:

```bash
powershell.exe -NoProfile -File "$(wslpath -w "$PWD/scripts/wsl-camera-speed-init.ps1")"
```

The helper finds the exact `Integrated Camera` name in `usbipd.exe state` and
resolves its current driver path. No USB instance ID, serial number, or Windows
bus ID is hardcoded. It treats the comma-separated device functions as separate
names, so `Integrated IR Camera` does not match `Integrated Camera`. To use a
different camera name, check `usbipd.exe list` and pass `-CameraName 'Exact Name'`.
Missing or ambiguous names and unattached devices are errors.

It sends `SUPUSB_IOCTL_GET_DEVICE` to the selected VBoxUSB interface. It does not
claim, reset, detach, or replace the driver. It succeeded without elevation on
the tested host, including a repeated invocation. Re-run after reattachment or
a driver restart.

The installed usbipd device-claim code omits this call. In the inspected VBoxUSB
source, the call initializes `fIsHighSpeed`, which controls isochronous transfer
scheduling. Without initialization, a USB 2 high-speed camera can be scheduled
as full speed. Before the call, 32-packet transfer completions were approximately
32 ms apart; afterward they were approximately 4 ms apart, matching 32 USB
high-speed microframes. Complete raw capture became possible immediately.

Sources: [usbipd device claim](https://github.com/dorssel/usbipd-win/blob/fe06088bb6916e6722bd66b03d245141ae8ec6c1/Usbipd/VBoxUsb.cs),
[VBoxUSB speed initialization and scheduling](https://github.com/VirtualBox/virtualbox/blob/main/src/VBox/HostDrivers/VBoxUSB/win/dev/VBoxUsbRt.cpp).
This is a local workaround for the tested driver combination, not an upstream
usbipd fix. The driver's speed flag itself was not read directly.

## Optional Linux bandwidth selection used in the latest preview

The latest 640×480 preview also used `UVC_QUIRK_FIX_BANDWIDTH` (`128`). It selected
alternate setting 10 (2,880 bytes per microframe), instead of the camera-requested
setting 11 (3,060). A short capture passed without damaged frames, but a longer
preview still had occasional loss. This does not eliminate the transport issue.

Stop capture first. Resolve the bound USB interface from the selected RGB
capture node. These commands
rebind only that Linux interface and restore the global parameter afterward:

```bash
sudo bash <<'SH'
set -euo pipefail
camera_node=video0
camera_iface=$(basename "$(readlink -f "/sys/class/video4linux/$camera_node/device")")
test -d "/sys/bus/usb/drivers/uvcvideo/$camera_iface"
quirks_param=/sys/module/uvcvideo/parameters/quirks
original_quirks=$(cat "$quirks_param")
trap 'printf "%s" "$original_quirks" > "$quirks_param"' EXIT
printf 128 > "$quirks_param"
printf '%s' "$camera_iface" > /sys/bus/usb/drivers/uvcvideo/unbind
printf '%s' "$camera_iface" > /sys/bus/usb/drivers/uvcvideo/bind
SH
```

Rediscover the capture node afterward. The quirk remains on the rebound camera
until it is rebound again with the original parameter or disconnected. To undo
it, repeat just the unbind and bind commands after the global parameter has been
restored. Do not persist this quirk system-wide: the 640×360 raw mode failed with
it. MJPEG is unaffected by this raw-format bandwidth calculation.

## Direct raw display probe

From the repository root:

```bash
GST_DEBUG_NO_COLOR=1 GST_DEBUG=v4l2allocator:7,glupload:6 \
  ./scripts/dmabuf-camera-probe.sh /dev/video0
```

The script runs YUY2 640×480 at a requested 30 fps:

```text
v4l2src io-mode=dmabuf → raw caps → identity drop-buffer-flags=corrupted
  → queue (2 buffers, discard oldest when full) → glimagesink sync=false
```

`identity` drops frames marked corrupt by the V4L2 driver and otherwise passes
buffers through unchanged. The queue limits accumulated delay. The sink displays
frames on arrival and disables last-sample retention. These settings preserve
the capture DMA buffers; they do not decode or copy pixels into another format.
See the [identity property documentation](https://gstreamer.freedesktop.org/documentation/coreelements/identity.html#identity:drop-buffer-flags).

Check allocator logs for `exported DMABUF`, full-frame `bytesused: 614400`, and
upload logs for `DirectDmabufExternal returned 1`. Confirm the whole image
visually. Negotiated caps alone are insufficient. Stop with Ctrl+C.

The UVC `nodrop` parameter was `1` on this host, so damaged frames reached
GStreamer. Dropping them prevents green regions and motion artifacts but cannot
recover missing frames. Increasing `blocksize`, adding timestamps, disabling
sink synchronization, or buffering cannot reconstruct missing USB payloads.

### Rendering backend

Include `glcontext:4` in `GST_DEBUG` to check `GL_RENDERER` alongside the upload
method. On this host, the default renderer is `llvmpipe` (Mesa 25.2.8, LLVM
20.1.2): software GL. Wayland/EGL, X11/EGL, and X11/GLX all selected it.

Setting `GALLIUM_DRIVER=d3d12` for the process enabled the Intel Arc 140V GPU,
but the camera pipeline then used the `Raw Data` uploader. The EGL DMA buffer
import modifiers extension was unavailable. That path uploads camera pixels
into new GL memory and does not meet this probe's direct-import requirement.
The script therefore keeps the verified default path. The successful
`DirectDmabufExternal` import must not be described as GPU-accelerated zero-copy.

The backend controls are documented by
[GStreamer](https://gstreamer.freedesktop.org/documentation/gl/gstgldisplay.html).
The default-to-software symptom is also reported in
[WSLg issue 1332](https://github.com/microsoft/wslg/issues/1332).

Limiting llvmpipe to two threads with `LP_NUM_THREADS=2` initially improved a
30-second test: one damaged frame and a longest import gap of 0.11 seconds,
compared with 71 damaged frames and 0.50 seconds in the following default-thread
test. However, a 60-second repeat had 119 damaged frames, about 26.2 imports/s,
and a 0.93-second maximum gap. The apparent improvement was not sustained, so
the script does not set this override. Short clean captures do not establish
reliable delivery; repeat longer measurements before adopting a tuning change.

## Working MJPEG capture control

```bash
GST_DEBUG_NO_COLOR=1 GST_DEBUG=v4l2allocator:7 \
  gst-launch-1.0 -v v4l2src device=/dev/video0 io-mode=dmabuf \
  ! image/jpeg,width=640,height=480,framerate=30/1 \
  ! jpegdec ! glimagesink
```

This captures compressed frames in DMA buffers, then decodes them into new image
memory. It verifies DMA buffer capture and usable video, but does not preserve
the original camera DMA buffer through display.

## Wayland display probe

`waylandsink` is supplied by Ubuntu's `gstreamer1.0-plugins-bad` package. Test
direct raw display with:

```bash
GST_DEBUG_NO_COLOR=1 GST_DEBUG=waylandsink:6,v4l2allocator:7 \
  gst-launch-1.0 -v v4l2src device=/dev/video0 io-mode=dmabuf \
  ! video/x-raw,format=YUY2,width=640,height=480,framerate=30/1 \
  ! waylandsink
```

On this WSLg instance, this fails negotiation. The sink reports
`Could not bind to zwp_linux_dmabuf_v1` and no supported DMA buffer formats.
A separate `WAYLAND_DEBUG=1` run confirmed that the compositor advertises
`wl_shm` but does not advertise `zwp_linux_dmabuf_v1`. The sink's general
DMA buffer support therefore cannot be used with this compositor.

The following camera control displays a complete image, confirmed visually:

```bash
gst-launch-1.0 -v v4l2src device=/dev/video0 io-mode=dmabuf \
  ! image/jpeg,width=640,height=480,framerate=30/1 \
  ! jpegdec ! videoconvert ! video/x-raw,format=BGRA ! waylandsink
```

Allocator logs confirmed camera DMA buffer export. JPEG decoding and BGRA
conversion produce new image memory, and this display uses Wayland shared
memory. It does not display the original camera DMA buffer directly. A BGRA
`videotestsrc` control also reached EOS successfully through `waylandsink`.

## Measured result on 2026-10-13

Environment: SunplusIT integrated camera `5986:2178`, Ubuntu 24.04 under WSL2,
kernel `6.18.33.2-microsoft-standard-WSL2`, GStreamer 1.24.2, WSLg 1.0.73.2,
usbipd `5.1.0-41` at `fe06088`, VBoxUSB 7.1.8.168469.

- Raw DMA buffer capture selected the `DirectDmabufExternal` GL importer, but
  only the top portion of the image was visible; most of the frame was green.
- Ordinary `io-mode=mmap` raw capture with CPU conversion produced the same
  corruption, placing the problem before GPU import.
- The smaller YUYV 640×360 mode also failed: direct `v4l2-ctl` capture reported
  error-marked buffers of approximately 8–32 KB, versus 460,800 bytes required
  for a complete frame. No complete frame was saved during the 10-second test.
  Requesting 5 fps was clamped to 30 fps by the camera.
- MJPEG 640×480 at 30 fps produced a complete image, confirmed visually with
  both mmap capture and DMA buffer capture. DMA buffer allocator logs confirmed
  exported file descriptors during the latter.
- After Windows speed initialization, raw DMA buffer capture and direct GL
  import produced a complete image, confirmed visually. A 10-second capture
  with the original bandwidth selection delivered 277 complete frames and
  seven error-marked frames before shutdown.
- With bandwidth quirk 128, a 10-second 640×480 capture delivered 292 complete
  frames and no error-marked frames before shutdown. The 60-second direct GL
  preview delivered about 26 imported frames/s on average, with 1,552 good and
  145 error-marked capture buffers. The longest gap between imports was about
  0.43 seconds. The user described it as almost perfect, with a few pauses.
- A temporary 125 ms manual-exposure diagnostic reduced capture to about 8 fps.
  Automatic exposure was restored (`auto_exposure=3`,
  `exposure_time_absolute=156`, `exposure_dynamic_framerate=1`). These controls
  need separate writes when restoring exposure before switching to auto mode.

The main truncation fault is addressed. Residual USB/UVC loss and occasional
pauses remain under investigation. The raw preview proves GStreamer camera
DMA buffer export and direct GL import on this host. It does not establish
zero-copy transport through Windows, USB/IP, the graphics driver, or the WSLg
compositor, and does not exercise OPK runtime DMA buffer support.
