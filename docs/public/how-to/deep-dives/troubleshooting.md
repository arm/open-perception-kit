---
sidebar_position: 9
sidebar_label: Troubleshooting
---

# Troubleshooting

Use this page when the quick-guide path is mostly complete but the browser, SSH, mDNS, or camera setup needs extra attention.

## Browser and WebRTC connection issues

Microsoft Edge, Firefox, and Safari are the suggested browsers for the Perception Experience Kit web UI.

If the web UI opens but the video stays black, check the browser mDNS settings:

- Edge: open `edge://flags/`, find `#enable-webrtc-hide-local-ips-with-mdns`, and disable it.
- Firefox: open `about:config`, find `media.peerconnection.ice.obfuscate_host_addresses`, and disable it.

If a Raspberry Pi URL such as `http://raspberrypi.local:9999` does not resolve, use the Raspberry Pi IP address instead. On the Pi, you can check the IP address with:

```bash
hostname -I
```

Then open:

```text
http://<raspberry-pi-ip>:9999
```

## Raspberry Pi SSH and mDNS

SSH must be enabled before VS Code can connect to the Raspberry Pi remote host.

During first setup, enable password authentication so the first connection does not depend on key setup being perfect:

```ini
PasswordAuthentication yes
```

After changing `/etc/ssh/sshd_config`, restart SSH:

```bash
sudo systemctl restart ssh
```

Test the connection from the host:

```bash
ssh pi@raspberrypi.local
```

If `raspberrypi.local` does not resolve, use the Pi IP address:

```bash
ssh pi@<raspberry-pi-ip>
```

### Empty `ssh` file creation

If SSH was enabled in Raspberry Pi Imager but does not start after boot, create an empty file named `ssh` in the root of the `bootfs` partition.

## Camera handling and checking

Camera source handling differs by platform and device. The quickest way to isolate issues is to validate the camera on the host before using it in an Perception Experience Kit pipeline.

### Raspberry Pi CSI cameras

Depending on your camera version, you may need to alter or add the following lines to `/boot/firmware/config.txt`:

```ini
# It may be enough to turn on auto detect for standard single-camera module use.
# For non-standard or multi-camera setups, try the following:
camera_auto_detect=0
dtoverlay=<cameratype1>,cam0
dtoverlay=<cameratype2>
```

| Camera name        | Camera type |
|--------------------|-------------|
| Camera Module v3   | imx708      |
| Camera Module v2   | imx219      |
| Camera Module v1.3 | imx219      |
| HQ Camera Module   | imx477      |

List CSI cameras:

```bash
rpicam-hello --list-cameras
```

For `libcamerasrc`, stable identifiers are the full camera names, not just `0` or `1`. Use the full name reported by `rpicam-hello --list-cameras` when building a GStreamer pipeline.

Example:

```bash
gst-launch-1.0 libcamerasrc -v camera-name="/base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a" ! video/x-raw,format=NV12,width=4608,height=2592 ! autovideosink
```

### USB cameras

List USB camera devices:

```bash
v4l2-ctl --list-devices
```

Example output:

```text
C922 Pro Stream Webcam (usb-xhci-hcd.0-1):
		/dev/video16
		/dev/video17
		/dev/media5
```

Use the matching `/dev/video*` device in the pipeline:

```bash
gst-launch-1.0 -e -v v4l2src device=/dev/video16 ! "video/x-raw,width=1280,height=720" ! autovideosink
```

For a USB camera and DMABuf:

```bash
gst-launch-1.0 v4l2src device=/dev/video16 io-mode=dmabuf ! "video/x-raw(memory:DMABuf),width=1280,height=720" ! filesink location=dmabuf_output.raw
```

To inspect a GStreamer element:

```bash
gst-inspect-1.0 <element-name>
```

For example:

```bash
gst-inspect-1.0 v4l2src
```

### WSL/Linux laptop webcam forwarding

For WSL, forward camera input into WSL first.

- Install [USBIPD](https://github.com/dorssel/usbipd-win/releases).
- Optionally install [WSL USB Manager](https://github.com/nickbeth/wsl-usb-manager/releases) for a GUI around USBIPD.
- Bind and attach the camera to WSL with WSL USB Manager.

If attaching the camera fails, disable the device in Windows Device Manager and try again. Windows can reserve the camera for background processes.

After the camera is visible in WSL/Linux, add the camera source to the pipeline and decode the stream before the models:

```json
"v4l2src device=/dev/video0 ! \"image/jpeg,width=1280,height=720,framerate=60/1\" !",
"jpegdec !",
```
