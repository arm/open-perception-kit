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

The [Raspberry Pi 5 test](#raspberry-pi-5-and-camera-module-3) uses a separate
Camera Module 3 and a direct KMS display path. It verifies DMA buffer import and
page flips on native Linux, independently of the WSL graphics limitations.

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

The script does not enforce direct import at the sink: `glimagesink` can fall
back to uploading pixels with another graphics backend. Successful pipeline
execution alone therefore does not establish zero-copy display. The measured
direct import means the original capture DMA buffer feeds the GL renderer;
it does not establish GPU rendering or direct scanout to the screen.

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

## Anker PowerConf C200

The external Anker camera was also attached and tested on 2026-10-13. It uses
USB 2 high-speed transport at 480 Mbit/s, like the integrated camera. Its raw
DMA buffer preview shows the full image correctly; the user reported generally
smooth movement with rare freezes.

### Attach and select the Anker

In Windows PowerShell **as Administrator**, resolve the connected camera by
name, share it, and attach it to WSL:

```powershell
$state = usbipd.exe state | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Cannot read USB state.' }
$cameras = @($state.Devices | Where-Object {
    ($_.Description -split ',\s*') -contains 'Anker PowerConf C200'
})
if ($cameras.Count -ne 1 -or -not $cameras[0].BusId) {
    throw 'Expected one connected Anker PowerConf C200.'
}
$camera = $cameras[0]
if (-not $camera.PersistedGuid) {
    usbipd.exe bind --busid $camera.BusId
    if ($LASTEXITCODE -ne 0) { throw 'Sharing the Anker camera failed.' }
}
if (-not $camera.ClientIPAddress) {
    usbipd.exe attach --wsl --busid $camera.BusId
    if ($LASTEXITCODE -ne 0) { throw 'Attaching the Anker camera failed.' }
}
```

Normal binding succeeded on the tested host. The first bind requires elevation;
later attachments of a shared device can run without it. Attachment makes the
camera available to WSL instead of Windows. See Microsoft's
[USB attachment instructions](https://learn.microsoft.com/en-us/windows/wsl/connect-usb).
The integrated camera can remain attached alongside it.

In WSL, rediscover the capture node after attachment:

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video4 --list-formats-ext
```

In this attachment, `/dev/video4` is the Anker video capture node and
`/dev/video5` is its metadata node. Replace `/dev/video4` in the commands below
with the discovered capture node. YUYV raw modes are 640×480, 640×360, and
320×240, all advertising 30 fps. Higher resolutions, through 2560×1440, are
advertised only for MJPEG and H.264 and require decoding for display.

### Run the direct raw probe

The same Windows speed initialization is needed for this camera on the tested
usbipd/VBoxUSB combination. From the WSL repository root:

```bash
powershell.exe -NoProfile -File "$(wslpath -w "$PWD/scripts/wsl-camera-speed-init.ps1")" \
  -CameraName 'Anker PowerConf C200'
GST_DEBUG_NO_COLOR=1 GST_DEBUG=v4l2allocator:7,glupload:6,glcontext:4 \
  ./scripts/dmabuf-camera-probe.sh /dev/video4
```

Use `sg video -c 'COMMAND'` if this shell has not picked up video-group
membership. No bandwidth quirk was applied to the Anker, and its exposure and
image controls were left at their existing values.

For the lower-bandwidth 640×360 mode used in the subsequent preview, run:

```bash
gst-launch-1.0 -v v4l2src device=/dev/video4 io-mode=dmabuf \
  ! video/x-raw,format=YUY2,width=640,height=360,framerate=30/1 \
  ! identity drop-buffer-flags=corrupted \
  ! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream \
  ! glimagesink sync=false enable-last-sample=false
```

### Measured Anker result

All tests used YUY2 raw capture. Every dequeued buffer below was complete and
had no V4L2 error flag: 614,400 bytes at 640×480, or 460,800 bytes at 640×360.

| Raw mode | Test | Complete frames | Delivered capture rate | Longest capture gap |
| --- | --- | ---: | ---: | ---: |
| 640×480 | Before speed initialization, 10-second capture | 31 | 3.24 fps | 0.32 s |
| 640×480 | After initialization, 250-frame capture | 250 | 22.54 fps | 0.21 s |
| 640×480 | After initialization, 60-second direct preview | 1,315 | 22.15 fps | 0.97 s |
| 640×360 | After initialization, 30-second direct preview | 762 | 26.03 fps | 0.74 s |

The 640×480 direct preview exported four DMA buffer file descriptors and used
`DirectDmabufExternal`. Display import averaged about 22.17 calls/s; the same
0.97-second pause was present in capture and import timestamps. This places
that pause before GL import. The Anker avoided the integrated camera's damaged
frames in this test, but did not deliver the requested 30 fps or eliminate
freezes. The user confirmed a correct full image with rare pauses.

The subsequent 640×360 preview also used `DirectDmabufExternal`, averaging
about 26.07 import calls/s. The user described this preview as perfect. Its
longest measured capture and import gaps were still about 0.74 seconds, so
the lower-bandwidth mode does not establish uninterrupted 30 fps delivery.

`GL_RENDERER` remained `llvmpipe`. This is proof of camera DMA buffer export and
direct EGL import, with the same software-rendering and end-to-end zero-copy
limitations described above. No OPK elements or decoder were used.

### Attempt to require GPU DMA buffer import

Full zero-copy display was **not achieved**. On the tested WSL stack, the Anker
exports usable DMA buffers, but the hardware renderer cannot import them and
the compositor does not expose a DMA buffer display protocol. These are separate
from the camera's residual capture pauses.

The stricter 60-frame probe makes the GL stages explicit and requires an external
texture at the upload boundary:

```bash
GST_DEBUG_NO_COLOR=1 GST_DEBUG=glcontext:4,glupload:6,v4l2allocator:7 \
  ./scripts/wsl-camera-gpu-import-probe.sh /dev/video4
```

It defaults to Mesa's `d3d12` renderer with Wayland/EGL/GLES and runs:

```text
v4l2src io-mode=dmabuf → YUY2 640×360 → drop corrupted frames → short queue
  → glupload → GLMemory, texture-target=external-oes
  → glcolorconvert → glimagesinkelement
```

The external texture requirement prevents a successful fallback to the raw
pixel uploader in the tested GStreamer 1.24.2 implementation. GStreamer can
still attempt that uploader before rejecting its output; this is a diagnostic,
not a guarantee that a failed run never maps or uploads pixels. `glcolorconvert`
allows the initial YUY2 caps to negotiate; the software control then renegotiates
to RGBA/external-oes at both its pads after direct import. Successful import
still does not prove that the camera buffer is scanned out unchanged. See the
[versioned upload implementation](https://github.com/GStreamer/gstreamer/blob/1.24.2/subprojects/gst-plugins-base/gst-libs/gst/gl/gstglupload.c).

Use the same script with the software renderer as a negotiation control:

```bash
GALLIUM_DRIVER=llvmpipe \
  GST_DEBUG_NO_COLOR=1 GST_DEBUG=glcontext:4,glupload:6,v4l2allocator:7 \
  ./scripts/wsl-camera-gpu-import-probe.sh /dev/video4
```

The software control must reach EOS with `DirectDmabufExternal returned 1` and
`GL_RENDERER: llvmpipe`. On this host, the default GPU attempt instead reports
`GL_RENDERER: D3D12 (Intel(R) Arc(TM) 140V GPU (16GB))`, attempts `Raw Data`, and
exits with `not-negotiated (-4)`. That failure is expected with the installed
backend; the working preview script remains available above.

The saved script's 60-frame software control exported four DMA buffer file
descriptors, made 61 direct import calls (including preroll), and used no
`Raw Data` uploads. The GPU attempt exported four DMA buffer descriptors,
made no direct imports, and attempted one raw upload before failing.

Measured on 2026-10-13 with GStreamer 1.24.2, Mesa 25.2.8, WSLg 1.0.73.2, and
kernel 6.18.33.2-microsoft-standard-WSL2:

| Boundary or route | Observation | Consequence |
| --- | --- | --- |
| Anker capture | V4L2 exports DMA buffer file descriptors with complete raw frames | Camera capture supports DMA buffer export |
| llvmpipe EGL | Both `EGL_EXT_image_dma_buf_import` and `EGL_EXT_image_dma_buf_import_modifiers` are present | Direct import works with software rendering |
| D3D12 EGL | Neither import extension is present | The Intel GPU backend cannot import these camera buffers through EGL |
| Direct `waylandsink` | Registry has `wl_shm`, no `zwp_linux_dmabuf_v1`; sink reports it cannot bind that protocol | No direct Wayland DMA buffer presentation on this compositor |
| Direct `kmssink` | No `/dev/dri`; sink fails with `Could not open DRM module` | No Linux DRM/KMS display device for direct scanout |

The EGL extension results came from `eglQueryString(EGL_EXTENSIONS)` after
initializing a Wayland EGL display for each renderer, independently of GStreamer.
The software control confirms that the strict caps can negotiate on an importer
that supports them. Merely switching sinks or changing queue settings does not
add the missing graphics interfaces. Microsoft's
[WSLg rendering description](https://github.com/microsoft/wslg#opengl-accelerated-rendering-in-wslg)
also describes system-memory interop with the compositor; GPU acceleration alone
does not establish an end-to-end zero-copy path.

Do not add a `memory:DMABuf` capsfilter immediately after this version of
`v4l2src`: it exports linear DMA buffers under ordinary raw caps, and that filter
fails negotiation here. GStreamer's
[DMA buffer design](https://gstreamer.freedesktop.org/documentation/additional/design/dmabuf.html)
allows linear DMA buffers without that caps feature. Check actual allocator and
importer logs instead.

A further zero-copy test needs a graphics stack that can import the camera's
linear YUY2 DMA buffers and present them without a CPU transfer. Native Linux
with a compatible GPU driver and Wayland compositor or KMS plane is a candidate,
but was not tested. Direct scanout additionally requires plane format support;
zero-copy import alone is insufficient. None of these tests establishes an
absence of copies during USB/IP transport or UVC frame assembly before V4L2
delivers a complete frame.

## Raspberry Pi 5 and Camera Module 3

The native Pi pipeline sends the Camera Module 3's hardware ISP output directly
to the VC4 KMS display engine:

```text
Camera Module 3 (imx708) → CSI capture / PiSP hardware ISP
  → libcamerasrc (NV12 DMA buffers) → identity → kmssink
```

There is no decoder, GL upload, `videoconvert`, OPK element, leaky queue, or
corrupt-frame filter. The probe preserves stream faults for inspection. The
tested zero-copy boundary is the completed ISP output buffer through KMS
scanout: KMS imports the camera buffer's file descriptor and presents that same
allocation. Sensor processing and conversion from Bayer to NV12 happen in the
Pi's camera hardware; this is not a claim that raw Bayer data is displayed.

### Reject CPU copy fallback

`libcamerasrc` supplies DMA buffers through its own allocator; it has no
`v4l2src`-style `io-mode` switch. Neither tested element offers a property that
forbids copying. The Pi script therefore builds and loads
[`dmabuf-no-copy.c`](../../scripts/private/dmabuf-no-copy.c) into its
`gst-launch-1.0` process using `LD_PRELOAD`. This guard intercepts
`gst_video_frame_copy()` and `gst_video_frame_copy_plane()` and exits with status
1 **before copying pixels**. These APIs cover the tested libcamera stride-copy
fallback, KMS import fallback, and KMS drain copy. The error is:

```text
DMA-BUF probe: CPU frame copy forbidden; stopping before copying.
```

The script checks that the guard loads before starting capture. A compiler or
loader failure also stops the probe. The guard is confined to this process and
its temporary build is removed on exit. It requires `cc`, `pkg-config`, and the
GStreamer video development files, already present in the tested development
container. Transfer the helper along with the shell script.

This blocks the known fallback functions, not arbitrary copies or CPU accesses
inside drivers or other libraries. Recheck the implementation and import logs
when changing plugin versions. The guard deliberately terminates immediately
on a forbidden copy; kernel file-descriptor cleanup releases the devices.

The regression test checks both copy APIs against a shared destination buffer,
successful execution without copying, and refusal to capture if the guard
cannot build or load:

```bash
python3 scripts/private/tests/test_dmabuf_no_copy.py
```

### Run the Pi probe

Use a native Pi GStreamer installation or an existing Pi development container
with the `libcamera` and `kms` plugins. Discover the camera name in that same
environment, since different libcamera versions can report different names:

```bash
gst-device-monitor-1.0 Video
```

Select the device containing `imx708` and copy its `camera-name` value. If the
runtime lacks `gst-device-monitor-1.0`, use the identifier in libcamera's
`Adding camera '...'` startup message. Discover the desired HDMI connector ID
on the host with `kmsprint -p`, or read its
`connector_id` under `/sys/class/drm/card*-HDMI-A-*`. Do not assume a fixed camera
path, DRM card number, or connector ID. With an available display and DRM access:

```bash
GST_DEBUG_NO_COLOR=1 \
  GST_DEBUG=2,libcamerasrc:6,identity:7,kmssink:7,kmsallocator:7,GST_PERFORMANCE:6 \
  GST_DEBUG_FILE=/tmp/rpi-dmabuf.gst.log \
  ./scripts/rpi-camera-dmabuf-probe.sh "$camera_name" "$connector_id"
```

The script requests NV12 640×480 at 30 fps and sends EOS after 5,400 frames,
roughly three minutes. It requires an available KMS connector and DRM master
access; an active desktop compositor may own that device. The script does not
change the host's connector configuration.

An optional third argument sets a different positive frame count, for example
`54000` for approximately thirty minutes of remote viewing. The default stays
5,400 frames; the copy guard remains active for the entire run.

The SSH test reused an existing repository development image with GStreamer
1.26.2, libcamera `0.7.2+rpt20260817`, and libpisp 1.7.0. The host ran Debian 13.5
and kernel `6.18.34+rpt-rpi-2712`. No packages were installed on the Pi. The
container entrypoint was bypassed to run only GStreamer, with network access
disabled. Device access covered `/dev/video*`, `/dev/media*`, `/dev/v4l-subdev*`,
`/dev/dma_heap`, and `/dev/dri`, with `/run/udev` mounted read-only. The existing
OPK containers and checkouts were not started or modified.

### Headless KMS test

Both HDMI connectors were disconnected. For each bounded test, the selected
connector's sysfs `status` was temporarily set to `on`. This exposed fallback
display modes, including 640×480. `kmssink force-modesetting=true` then drove the
primary display plane without a Wayland compositor or SSH display forwarding.
After the process exited, `status` was restored to `detect` in cleanup; the
connector returned to disconnected and disabled. No boot configuration or
persistent display setting was changed.

The kernel implements these runtime connector overrides in
[DRM sysfs](https://github.com/raspberrypi/linux/blob/rpi-6.18.y/drivers/gpu/drm/drm_sysfs.c).
Only use a headless override on a connector confirmed disconnected and inactive,
and restore detection after stopping capture. A connected monitor does not need
this override. Successful page flips without a monitor verify the display
engine path, not the visible image, colour accuracy, or physical HDMI output.

### Run the camera probe with a TCP preview

The camera and its remote preview are started by the **same probe command**.
Pass a TCP port as the fourth argument to enable streaming:

```bash
./scripts/rpi-camera-dmabuf-probe.sh "$camera_name" "$connector_id" 54000 5000
```

This runs the camera for approximately thirty minutes. Omitting the fourth
argument runs only the guarded camera/display test. An available connector is
still required; for a Pi without a monitor, use the temporary headless connector
override described above and restore detection after the probe exits.

The [probe](../../scripts/rpi-camera-dmabuf-probe.sh) contains both GStreamer
pipelines and manages their lifetime:

```text
libcamerasrc → NV12 camera DMA buffers → identity → guarded kmssink → KMS → HDMI
                                                                    │
                           same connector's active framebuffer ─────┘
                                      ↓ read-only CPU snapshot
                 rawvideoparse → videoconvert → jpegenc → multipartmux
                                      ↓ tcpserversink on 127.0.0.1
                                SSH tunnel → laptop GStreamer
```

The first pipeline captures the camera at a requested 30 fps and imports its
original DMA buffers directly into KMS. The second samples the active
framebuffer at up to ten frames per second, then copies and encodes that
readback for transport. No converter, encoder, inference element, or tee is
inserted in the camera/display path. The copy guard applies to that first
pipeline; the preview's CPU read and conversion are intentional.

The internal [readback helper](../../scripts/private/kms-snapshot.c) opens the
same `vc4` driver as `kmssink`, resolves the requested connector to its active
CRTC and NV12 plane, and exports that framebuffer with `drmModeGetFB2` and
`drmPrimeHandleToFD`. It maps the actual DMA-BUF allocation length read-only,
brackets CPU reads with `DMA_BUF_IOCTL_SYNC`, and packs visible NV12 rows. The
probe waits for this camera framebuffer during startup; the user does not
start another camera process or manually select a plane. Camera EOS, interruption,
or failure stops the preview processes. Preview failure stops the camera probe
and returns a failure rather than leaving a partial test running.

Streaming requires `cc`, `pkg-config`, libdrm development files, and the
GStreamer elements above, already present in the tested development image.
Transfer `scripts/private/kms-snapshot.c` along with the probe and its copy guard.
For a container, expose the camera and DRM devices as above, use host networking,
and add `--cap-add=SYS_ADMIN`. Run as root so DRM exposes framebuffer handles.
The helper does not acquire DRM master, modeset, or write the camera buffer.
The TCP sink binds only to loopback and disconnects slow clients rather than
accumulating unlimited buffers.

On the laptop, open an SSH tunnel:

```bash
ssh -N -o ExitOnForwardFailure=yes \
    -L 127.0.0.1:15000:127.0.0.1:5000 <user>@<pi-ip>
```

In another laptop terminal, start the receiving pipeline:

```bash
./scripts/view-kms-stream.sh 15000
```

The receiver uses `tcpclientsrc ! multipartdemux ! jpegdec ! videoconvert !
autovideosink sync=false`. On Windows with WSLg, run it inside WSL with the
normal display environment. Stop the probe, receiver, and tunnel with Ctrl+C
when finished. Stop the probe before restoring a forced connector to `detect`.

The readback supports the probe's linear NV12 640×480 image with both planes in
one allocation. It respects row padding and plane offsets and uses the tested
camera's BT.709 colorimetry. Unsupported layouts and out-of-bounds planes are
rejected. The existing probe test file covers the guard, preview lifecycle,
argument validation, and readback layout:

```bash
python3 scripts/private/tests/test_dmabuf_no_copy.py
```

On 2026-10-14, a guarded three-minute Pi 5 run delivered 5,400 frames and 5,401
direct DMA-BUF import/display calls including preroll, reached EOS, and reported
no copy attempts or GStreamer errors while a complete KMS image was captured.
The connector was restored to disconnected/disabled afterward. A live readback
stream subsequently reached the WSL laptop through SSH: its display receiver
reached PLAYING and an independent receiver decoded three complete 640×480
frames. Kernel driver tracing was not repeated for these visual checks.

The combined probe subsequently passed a 900-frame run with 901 direct KMS
imports/display calls, no copy attempts or GStreamer errors, and three received
preview frames. It exited zero at EOS, closed the TCP listener, and allowed the
headless connector override to be restored.

Stock FFmpeg 7.1.5 `kmsgrab` found the correct framebuffer but failed to map it:
its NV12 size calculation used full image height for the chroma plane, requesting
614,400 bytes from a 475,136-byte allocation. Both `hwdownload` and direct
`hwmap` failed with `EINVAL`. The helper uses the actual allocation length.
See the tested [FFmpeg implementation](https://github.com/FFmpeg/FFmpeg/blob/n7.1.5/libavdevice/kmsgrab.c).

This preview reads the selected scanout plane, not display-engine writeback or
physical HDMI output. It excludes hardware plane transforms, composition,
output colour processing, and monitor behaviour. The camera can reuse an
allocation while it is being read: DMA cache synchronization does not freeze
capture or synchronize the snapshot to a page flip. JPEG adds lossy compression.
Use the preview to inspect image content alongside the guarded import/display
evidence, not as a tear-free recording or performance measurement. A normal
`opksink` browser preview or capture before `kmssink` does not establish this
same display-path result.

### Verify zero-copy and stream health

The script rejects the tested copy fallbacks. Still check the actual buffer
imports and stream health, especially after upgrades. In the GStreamer log,
require a DMA buffer import for every display
call: `Found a dmabuf`, a new or cached `KMS mem` association, and `displaying fb`.
Check that neither `frame copy` nor `draining` occurs and that `libcamerasrc`
does not report `need to copy frame`. Enable `GST_PERFORMANCE` as above to make
the sink's copy fallback visible. See the versioned
[KMS sink implementation](https://github.com/GStreamer/gstreamer/blob/1.26.2/subprojects/gst-plugins-bad/sys/kms/gstkmssink.c)
and [libcamera source](https://github.com/libcamera-org/libcamera/blob/v0.7.2/src/gstreamer/gstlibcamerasrc.cpp).

Inspect `identity` buffer records for 460,800 bytes, continuous sequence offsets,
monotonic timestamps, and absent corruption flags. Compute frame rate and maximum
gaps from timestamps rather than the requested caps. The tested libcamera plugin
uses driver frame sequence numbers for offsets, but does not propagate every
driver error status to GStreamer's corruption flag. Check kernel V4L2 dequeue
events as well before concluding that the driver returned no errored buffers.

A separate 60-frame system-call trace verified successful
`DRM_IOCTL_PRIME_FD_TO_HANDLE`, framebuffer creation, and 61 page flips, including
preroll. Four camera DMA buffer allocations were reused, with two NV12 planes
sharing each allocation. Libcamera mapped the buffers at startup; mapping an
allocation into an address space does not itself copy its pixels. The direct
imports prove that the original ISP output allocations reached KMS; these tests
do not audit every CPU access inside the camera stack.

Two three-minute runs before adding the copy guard passed on 2026-10-13:

| Run | Delivered frames | Measured rate | Longest sensor timestamp gap | Longest GStreamer arrival gap | Longest KMS completion gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| GStreamer diagnostics | 5,400 | 30.013 fps | 33.435 ms | 43.048 ms | Not traced |
| Kernel trace repeat | 5,400 | 30.013 fps | 33.343 ms | 41.156 ms | 50.054 ms |

Both runs had continuous sequence offsets 0–5,399, expected buffer sizes, and
5,401 direct imports/display calls including preroll. Neither had GStreamer
errors, source or sink copy fallbacks, or sink drain copies.

The second run used a separate tracefs instance with `v4l2/v4l2_dqbuf`,
`vb2/vb2_dqbuf`, and `drm/drm_vblank_event_delivered` enabled. All 59,411 driver
dequeues across the camera/ISP queues had no `ERROR` flags and no sequence gaps.
The ISP's selected NV12 output produced 5,401 buffers of 460,800 bytes, including
the extra capture that triggers the identity element's EOS. For multi-planar
devices, the top-level `v4l2_dqbuf` byte count was zero: the actual payload size
was obtained by matching the preceding `vb2_dqbuf` event in the same thread and
with the same buffer index. A zero top-level count here does not mean an empty
frame. Discover the output node from the allocator trace, rather than assuming
that `/dev/video0` is the processed image.

There were exactly 5,401 KMS completion events for the sink's DRM file, separate
from five setup/cleanup events with a null file. All 124,228 trace entries were
retained, with zero overruns or dropped trace events. The trace instance was
disabled and removed after collection. Assertions checked these counts, sequence
continuity, payload sizes, copy-path logs, successful EOS, and connector cleanup.

Startup logs included unsupported `Nc30`/`Nc12` enumeration warnings and a KMS
default-fd warning; NV12 capture and import succeeded. The forced connector also
produced HDMI audio `Unknown ELD version 0` messages without a monitor's EDID.
No camera or ISP error was reported in the kernel log during these runs.

This establishes a working zero-copy ISP-output-to-KMS configuration on the
tested Pi. It does not verify the visible image or guarantee the absence of
faults in longer runs, other modes, or other plugin/driver versions. Recheck the
import and copy-path evidence when changing the pipeline or platform.

### Copy guard validation

On 2026-10-14, the four regression tests passed in both the development container
and the Pi container. Live tests exercised both actual element fallbacks:

| Fallback | Deliberate trigger | Without guard | With guard |
| --- | --- | --- | --- |
| `libcamerasrc` stride copy | NV12 642×480 with `identity drop-allocation=true` before `fakesink` | Copied and reached EOS | Exit 1 before the copy |
| `kmssink` import copy | `videotestsrc` NV12 640×480 with `identity drop-allocation=true` before KMS | Copied and reached EOS | Exit 1 before the copy |

The exact guarded camera script then delivered 5,400 complete frames at
30.013 fps, with sequence offsets 0–5,399 and 5,401 direct DMA buffer
imports/display calls including preroll. It reached EOS with exit 0 and no copy
attempts or GStreamer errors. The longest sensor timestamp gap was 33.326 ms;
the longest GStreamer arrival gap was 42.127 ms. The temporary connector override
was restored, with no remaining test containers or capture/display owners.
Kernel tracing and visual inspection were not repeated for this guard change.
