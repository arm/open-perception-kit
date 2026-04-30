---
sidebar_position: 8
sidebar_label: Raspberry Pi 5
---

# Raspberry Pi 5: Assembly and Installation Guide
> Note: These instructions are validated for Raspberry Pi 5. Earlier Raspberry Pi versions may require different packages or may not be fully supported.
> This whole document should be followed outside the container on the remote target to enable seamless work with the development or deployment container.

## What will you learn from this documentation?

If you follow this page successfully, you will learn how to assemble a supported Raspberry Pi 5 setup, install the required remote host packages, validate the attached camera, and prepare the device for AMP container workflows.

At the end of this page, you should have a Raspberry Pi 5 on your desk that is ready for AMP development or deployment, with SSH access working and camera-related host validation completed.

## Components

In order to run the project the following components are needed:

- [Raspberry Pi 5 16GB](https://www.raspberrypi.com/products/raspberry-pi-5/)
	- The 8GB version should work as well, but it is not tested at the moment.
- [AI HAT+](https://www.raspberrypi.com/products/ai-hat/)
	- Use this with the `RPI5 H8 amp-dev-forge` / `amp-dev-rpi5-h8` container path.
- Supported Hailo 10 accelerator
	- Use this with the `RPI5 H10 amp-dev-forge` / `amp-dev-rpi5-h10` container path.
	- The Hailo 10 kernel and PCIe driver packages remain host-side. The container only installs the Hailo 10 user-space stack.
- USB or CSI camera
	- [Camera Module v3](https://www.raspberrypi.com/products/camera-module-3/)
	- USB camera (project tested with Lenovo C920 Pro)
- Class 10 SD Card with at least 32GB capacity
- (Optional)Raspberry Pi Active Cooler.
- (Optional) [M.2 HAT](https://www.raspberrypi.com/products/m2-hat-plus/) with compatible SSD
- (Optional) [Monitor](https://www.raspberrypi.com/products/raspberry-pi-monitor/)
- (Optional) PCIe splitter
	- Only needed if the Ai Hat and SSD HAT are intended to be used at the same time.

## Assembly

1.  Attach spacers to the Raspberry Pi 5.
2.  Mount the AI HAT+ 2 onto the GPIO header.
3.  Connect the PCIe ribbon cable:
    -   From the Raspberry Pi 5 PCIe port to the HAT
    -   Ensure copper contacts face **up on the HAT side**
4.  Install the heatsink onto the Hailo chip on the HAT.
5.  Connect the power supply.

## Installation

Follow these tutorials to install the latest (trixie) OS and assemble your Pi. If possible, use a wired connection during installation.

- [Basic instructions](https://www.raspberrypi.com/documentation/computers/getting-started.html)
- [M.2 Installation](https://www.raspberrypi.com/documentation/accessories/m2-hat-plus.html)

SSH can also be configured using the installer. Sometimes, even when SSH is enabled, it may not work out of the box after boot. In that case, create an empty file named `ssh` in the root folder of `bootfs`.

### Update System

``` bash
sudo apt update
sudo apt full-upgrade -y
sudo rpi-eeprom-update -a
```

### Enable PCIe Gen 3

``` bash
sudo raspi-config
```
-   Navigate to: Advanced Options → PCIe Speed → Enable Gen 3

### Additional packages
```bash
sudo apt-get update
sudo apt-get install -y git docker.io v4l-utils raspi-utils-core raspi-utils-dt
sudo apt-get install rpicam-apps libcamera-dev libcamera-doc libcamera-tools \
  gstreamer1.0-tools gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-gl \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
  gstreamer1.0-libcamera \
  code  libcairo2-dev libssl-dev
```

Alternatively, after booting from the SD card, you can copy its contents to the M.2 drive using the "SD Card Copier" tool on the Pi.

## Testing the Cameras

Depending on your camera version, you might need to alter/add the following lines to your `/boot/firmware/config.txt`:

```ini
# It may be enough to turn on auto detect for standard single camera module use.
# For non-standard or multi-camera, try the following:
camera_auto_detect=0
dtoverlay=<cameratype1>,cam0
dtoverlay=<cameratype2>
```

| Camera name         | Camera type |
|---------------------|-------------|
| Camera Module v3    | imx708      |
| Camera Module v2    | imx219      |
| Camera Module v1.3  | imx219      |
| HQ Camera Module    | imx477      |

## Hailo installation

The `RPI5 H10 amp-dev-forge` and `RPI5 H8 amp-dev-forge` container only adds the Hailo 10 user-space packages. Kernel or PCIe driver packages such as `hailort-pcie-driver` or `h10-hailort-pcie-driver` stay on the remote host.

### Hailo8

If you are using the Hailo 8 / AI HAT+ path, install the required tools:

```sh
sudo apt-get update
sudo apt-get install dkms
sudo apt-get install hailo-all
sudo reboot
```

```sh
ls /dev/hailo*
```

``` bash
hailortcli fw-control identify
```

-   Confirm output includes: Device Architecture: HAILO8
Example:
``` bash
Executing on device: 0001:03:00.0
Identifying board
Control Protocol Version: 2
Firmware Version: 4.23.0 (release,app,extended context switch buffer)
Logger Version: 0
Board Name: Hailo-8
Device Architecture: HAILO8
```

### Hailo10

If you are using the Hailo 10 path, install the required host-side Hailo 10 driver stack first.

``` bash
sudo apt install dkms
sudo apt install hailo-h10-all
sudo reboot
```

### Verify Installation

After the host-side Hailo 10 setup, confirm that the device nodes exist:

```sh
ls /dev/hailo*
```

``` bash
hailortcli fw-control identify
```

-   Confirm output includes: Device Architecture: HAILO10H
Example:
``` bash
pi@rpi-5:~ $ hailortcli fw-control identify
Executing on device: 0001:03:00.0
Identifying board
Control Protocol Version: 2
Firmware Version: 5.1.1 (release,app)
Logger Version: 0
Device Architecture: HAILO10H
```

## Quality of life on the Pi

For smoother mouse handling, add the following to the end of `/boot/firmware/cmdline.txt` (separated with a space):

```
usbhid.mousepoll=0
```

## Docker Installation

See: [Debian | Docker Docs](https://docs.docker.com/engine/install/debian/)

## Raspberry Pi devcontainer targets

The repository now ships two Raspberry Pi-specific development container targets:

- `RPI5 H8 amp-dev-forge` -> service `amp-dev-rpi5-h8` -> Hailo 8 / AI HAT+ path
- `RPI5 H10 amp-dev-forge` -> service `amp-dev-rpi5-h10` -> Hailo 10 / AI HAT+ 2 path

The matching full-demo presets are `config/pipelines/02-full-onnx-hailo8.json`, `config/pipelines/03-full-onnx-hailo8l.json` and `config/pipelines/04-full-onnx-hailo10.json`.

When you validate the accelerated path from `amp-menu`, select the matching Hailo preset for the accelerator installed on the Pi.

![Selecting a Raspberry Pi Hailo pipeline](../../../static/img/21-raspberry-hailo-pipeline1.png)

![Running the selected Raspberry Pi Hailo pipeline](../../../static/img/22-raspberry-hailo-pipeline2.png)

Both Raspberry Pi containers use host networking.
Before either container is created, `.devcontainer/platform_init.sh` runs on the host and generates the camera, audio, NPU, and shared-memory docker-compose overrides for the selected service.
That is why the Pi-hosted UI and documentation stay reachable at `http://raspberrypi.local:9999` and `http://raspberrypi.local:8080`.

## SSH and VS Code

To set up an SSH connection with VS Code, first enable password authentication in `/etc/ssh/sshd_config` by changing the following line:

```ini
PasswordAuthentication yes
```

Then follow the [VS Code Remote SSH tutorial](https://code.visualstudio.com/docs/remote/ssh).

> **Note for Mac users:** Grant VS Code access to the local network in **Settings → Privacy & Security → Local Network**, otherwise the remote connection will fail.

Test the SSH connection before opening the repository through VS Code.

```bash
ssh pi@raspberrypi.local
```

![Terminal testing an SSH connection to the Raspberry Pi](../../../static/img/15-ssh-test.png)

After the first negotiation, your key will be stored on the Pi and you can switch back to `PasswordAuthentication no`.

## Checking Cameras

### CSI Cameras

```sh
rpicam-hello --list-cameras
```

Example output:

```
Available cameras
-----------------
0 : imx708 [4608x2592 10-bit RGGB] (/base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a)
	Modes: 'SRGGB10_CSI2P' : 1536x864 [120.13 fps - (768, 432)/3072x1728 crop]
							 2304x1296 [56.03 fps - (0, 0)/4608x2592 crop]
							 4608x2592 [14.35 fps - (0, 0)/4608x2592 crop]

1 : imx219 [3280x2464 10-bit RGGB] (/base/axi/pcie@1000120000/rp1/i2c@70000/imx219@10)
	Modes: 'SRGGB10_CSI2P' : 640x480 [206.65 fps - (1000, 752)/1280x960 crop]
							 1640x1232 [41.85 fps - (0, 0)/3280x2464 crop]
							 1920x1080 [47.57 fps - (680, 692)/1920x1080 crop]
							 3280x2464 [21.19 fps - (0, 0)/3280x2464 crop]
		   'SRGGB8' : 640x480 [206.65 fps - (1000, 752)/1280x960 crop]
					 1640x1232 [83.70 fps - (0, 0)/3280x2464 crop]
					 1920x1080 [47.57 fps - (680, 692)/1920x1080 crop]
					 3280x2464 [21.19 fps - (0, 0)/3280x2464 crop]
```

> **Note:** For `libcamerasrc`, the stable identifiers are not `0` and `1`; they are the full camera names such as `/base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a` and `/base/axi/pcie@1000120000/rp1/i2c@70000/imx219@10`. Use supported formats and resolutions when building the GStreamer pipeline.

### USB Cameras

Use the first device (e.g., `video16` on my machine) for pipelines:

```sh
v4l2-ctl --list-devices
```

Example output:

```
C922 Pro Stream Webcam (usb-xhci-hcd.0-1):
		/dev/video16
		/dev/video17
		/dev/media5
```

## Final validation of the system

```sh
# Check the pads of a GStreamer element
gst-inspect-1.0 v4l2src

# Launch a pipeline with CSI camera
gst-launch-1.0 libcamerasrc -v camera-name="/base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a" ! video/x-raw,format=NV12,width=4608,height=2592 ! autovideosink

# Or with a USB camera
gst-launch-1.0 -e -v v4l2src device=/dev/video16  ! "video/x-raw,width=1280,height=720" ! autovideosink

# Or with a USB camera and DMABuf
gst-launch-1.0 v4l2src device=/dev/video16 io-mode=dmabuf ! "video/x-raw(memory:DMABuf),width=1280,height=720" ! filesink location=dmabuf_output.raw
```
- To troubleshoot and check the details of a GStreamer element, use:

```sh
gst-inspect-1.0 <element-name>
```

## What should you have at the end of this document?

By the end of this page, you should have:

- a Raspberry Pi 5 assembled with the intended AI and camera hardware
- the required host packages installed
- working SSH access for VS Code remote use
- at least one validated camera path using `rpicam-hello`, `v4l2-ctl`, or `gst-launch-1.0`

Success looks like this: the Pi boots with the expected hardware, the camera is discoverable, simple camera pipelines run, and the board is ready for the AMP container workflow.
