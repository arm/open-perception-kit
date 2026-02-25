# Raspberry Pi 5: Assembly and Installation Guide

## Components

- [Raspberry Pi 5 16GB](https://www.raspberrypi.com/products/raspberry-pi-5/)
- [AI HAT+](https://www.raspberrypi.com/products/ai-hat/)
- [M.2 HAT](https://www.raspberrypi.com/products/m2-hat-plus/)
- [Camera Module v3](https://www.raspberrypi.com/products/camera-module-3/)
- microSD Card
- [Monitor](https://www.raspberrypi.com/products/raspberry-pi-monitor/)
- PCIe splitter (if both HATs are needed at the same time)
- Logitech C922 Pro UVC camera

> **Note:** DMABuf is not yet supported on the PI5 for CSI cameras, so a UVC camera might have better performance.

## Installation

Follow these tutorials to install and assemble your Pi. If possible, use a wired connection during installation:

- [Basic instructions](https://www.raspberrypi.com/documentation/computers/getting-started.html)
- [M.2 Installation](https://www.raspberrypi.com/documentation/accessories/m2-hat-plus.html)

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

## Additional Packages and Settings

```sh
sudo apt-get install libcamera-apps libcamera-dev libcamera-doc libcamera-tools \
  gstreamer1.0-tools gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-gl \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
  gstreamer1.0-libcamera \
  ffmpeg \
  code
```

For smoother mouse handling, add the following to the end of `/boot/firmware/cmdline.txt` (separated with a space):

```
usbhid.mousepoll=0
```

## Docker Installation

See: [Debian | Docker Docs](https://docs.docker.com/engine/install/debian/)

## SSH and VS Code

To set up SSH connection with VS Code, first enable password authentication in `/etc/ssh/sshd_config` by changing the following line:

```ini
PasswordAuthentication yes
```

Then follow the tutorial: https://code.visualstudio.com/docs/remote/ssh

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

> **Note:** Your camera name is not 0 and 1, but rather `/base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a` and `/base/axi/pcie@1000120000/rp1/i2c@70000/imx219@10`. Supported formats and resolutions should be used later during GStreamer pipeline creation.

### UVC Cameras

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

## GStreamer Pipelines

```sh
# Check the pads of a GStreamer element
gst-inspect-1.0 v4l2src

# Launch a pipeline with CSI camera
gst-launch-1.0 libcamerasrc -v camera-name="/base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a" ! video/x-raw,format=NV12,width=4608,height=2592 ! autovideosink

# Or with a UVC camera
gst-launch-1.0 -e -v v4l2src device=/dev/video16  ! "video/x-raw,width=1280,height=720" ! autovideosink

# Or with a UVC camera and DMABuf
gst-launch-1.0 v4l2src device=/dev/video16 io-mode=dmabuf ! "video/x-raw(memory:DMABuf),width=1280,height=720" ! filesink location=dmabuf_output.raw
```

## Debugging

When connected through SSH, `autovideosink` should open an OpenGL window on your local machine. You can also check your output with a UDP stream (e.g., in VLC: open Media stream and type `udp://@IP:5000` and hit play):

```sh
gst-launch-1.0 -e -v v4l2src device=/dev/video16  ! "video/x-raw,width=1280,height=720" ! \
	videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast ! mpegtsmux ! \
	udpsink host="$IP" port=5000 sync=false async=false
```

If stuck, you can pass the `-v` option for `gst-launch-1.0` for extra log. For additional verbosity, set different variables based on your needs:

```sh
GST_DEBUG_FILE=alloc.log GST_DEBUG=2,caps:6,allocation:7,glupload:6,gl:6 GST_GL_PLATFORM=egl GST_GL_API=gles2 \
	gst-launch-1.0 v4l2src device=/dev/video16 io-mode=dmabuf ! "video/x-raw(memory:DMABuf),width=1280,height=720" ! filesink location=dmabuf_output.raw
```

## Additional Resources

- To check DMABuf, use the script called `lazer` under [amp-dev-forge](https://github.com/Arm-Debug/amp-dev-forge)
- The mentioned functionalities are also available in Docker with additional examples: [amp-dev-forge](https://github.com/Arm-Debug/amp-dev-forge)
Assembly and Installation
Components
[Raspberry Pi 5 16GB](https://www.raspberrypi.com/products/raspberry-pi-5/)
[AI HAT+](https://www.raspberrypi.com/products/ai-hat/)
[M.2 HAT](https://www.raspberrypi.com/products/m2-hat-plus/)
[Camera Module v3](https://www.raspberrypi.com/products/camera-module-3/)
microSD Card
[Monitor](https://www.raspberrypi.com/products/raspberry-pi-monitor/)
PCIe splitter
If both HAT is needed at the same time
Logitech C922 Pro UVC camera
DMABuf is not yet supported on the PI5 for CSI cameras so a UVC camera might have a better performance.
Installation

Follow the following tutorial to install and assemble your PI. If possible during installation use a cable connection.
[Basic instructions](https://www.raspberrypi.com/documentation/computers/getting-started.html)
[M.2 Installation](https://www.raspberrypi.com/documentation/accessories/m2-hat-plus.html)
Alternatively after booting from the SD card you can also copy the content of the SD card to the M.2 drive with the "SD Card Copier" tool on the Pi.

Testing the cameras

Depending on your camera version you might have to alter/add the following lines to your  /boot/firmware/config.txt 

```
# It may be enough to turn on auto detect for standard single camera module use.
# For non standard or multi camera I had more luck with the following:
camera_auto_detect=0
dtoverlay=<cameratype1>,cam0
dtoverlay=<cameratype2>
```

Camera name	Camera type
Camera Module v3	imx708
Camera Module v2	imx219
Camera Module v1.3	imx219
HQ Camera Module	imx477
Additional packages and settings




```
sudo apt-get install libcamera-apps libcamera-dev libcamera-doc libcamera-tools \
  gstreamer1.0-tools gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-gl \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
  gstreamer1.0-libcamera \
  ffmpeg \
  code
```

For more smooth mouse handling add the following to the end of /boot/firmware/cmdline.txt separated with a space.

```
usbhid.mousepoll=0
```

Docker installation: Debian | Docker Docs

SSH and vscode

To set up ssh connection with vscode first enable password authentication in /etc/ssh/sshd_config by changing the following line:
```
PasswordAuthentication yes
```
Then follow the tutorial: https://code.visualstudio.com/docs/remote/ssh
After the first negotiation your key will be stored on the PI and you can switch back to PasswordAuthentication no

Check cameras
CSI cameras

```
edge-ai@raspberrypi:~/gstreamertest $ rpicam-hello --list-cameras
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
Keep in mind that your camera neme is not 0 and 1 but /base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a and /base/axi/pcie@1000120000/rp1/i2c@70000/imx219@10 instead.
Supported formats and resolutions should be used later on during gstreamer pipeline creation.

UVC cameras

Use the first device (video16 on my machine) for pipelines

```
edge-ai@raspberrypi:~/gstreamertest $ v4l2-ctl --list-devices
C922 Pro Stream Webcam (usb-xhci-hcd.0-1):
        /dev/video16
        /dev/video17
        /dev/media5
edge-ai@raspberrypi:~/gstreamertest $ 
```

Gstreamer pipelines

```
# You can check the pads of a gstreamer element with
gst-inspect-1.0 v4l2src
# launch a pipeline with CSI camera
gst-launch-1.0 libcamerasrc -v camera-name="/base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a" ! video/x-raw,format=NV12,width=4608,height=2592 ! autovideosink
# or with a UVC camera
gst-launch-1.0 -e -v v4l2src device=/dev/video16  ! "video/x-raw,width=1280,height=720" ! autovideosink
# or with a UVC camera and DMABuf
v4l2src device=/dev/video16 io-mode=dmabuf ! "video/x-raw(memory:DMABuf),width=1280,height=720" ! filesink location=dmabuf_output.raw
```

Debugging

When connected through SSH audovideosink should open an OpenGL window on your local machine.
But you can also check your output with an UDP stream. You can check this stream in VLC for example. Simply open a Media stream and type udp://@IP:5000 and hit play
```
gst-launch-1.0 -e -v v4l2src device=/dev/video16  ! "video/x-raw,width=1280,height=720" ! 
    videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast ! mpegtsmux !
    udpsink host="$IP" port=5000 sync=false async=false
```

If stuck you can pass the -v option for gst-launch-1.0 for extra log. For additional verbosity set different variables based on your needs. 
```
   GST_DEBUG_FILE=alloc.log GST_DEBUG=2,caps:6,allocation:7,glupload:6,gl:6 GST_GL_PLATFORM=egl GST_GL_API=gles2 4l2src device=/dev/video16 io-mode=dmabuf ! "video/x-raw(memory:DMABuf),width=1280,height=720" ! filesink location=dmabuf_output.raw
```

To check DMABuf use the script called lazer under https://github.com/Arm-Debug/amp-dev-forge
The mentioned functionalities are also available in docker with additional examples: https://github.com/Arm-Debug/amp-dev-forge

