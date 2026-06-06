---
sidebar_position: 9
sidebar_label: Hailo Inference
---

# Run Hailo Inference On Raspberry Pi 5

Use this guide when you have a Raspberry Pi 5 with a supported Hailo 8 AI HAT,
Hailo 8L hardware with matching compiled models, or a supported Hailo 10
accelerator.

## Install The Hailo Host Stack

Run these commands in the **Raspberry Pi shell**, not inside the container.

For a Hailo 8 AI HAT, install the Hailo 8 stack:

```bash
sudo apt-get install -y dkms
sudo apt-get install -y hailo-all
sudo reboot
```

For a supported Hailo 10 accelerator, install the Hailo 10 stack:

```bash
sudo apt-get install -y dkms
sudo apt-get install -y hailo-h10-all
sudo reboot
```

After the reboot, reconnect to the Raspberry Pi.

## Check The Installation

Run in the **Raspberry Pi shell**:

```bash
ls /dev/hailo*
hailortcli fw-control identify
```

Expected result:

- `ls /dev/hailo*` shows at least one Hailo device.
- `hailortcli` prints the Hailo device architecture, such as `HAILO8` or
  `HAILO10H`.

If these checks fail, fix the host Hailo installation before starting the PEK
container.

## Prepare PEK

From the PEK repository on the Raspberry Pi, run:

```bash
./scripts/quick_start.sh
./scripts/download_videos.sh
./scripts/build.sh
```

`./scripts/quick_start.sh` starts the matching Raspberry Pi container and passes through
the Hailo device when it is visible on the host. `./scripts/build.sh` builds 
PEK inside that container. `./scripts/download_videos.sh` downloads
the stock videos, necessary for running the example pipelines.

## Run A Hailo Pipeline

For Hailo 8, run:

```bash
./scripts/run.sh 02-full-onnx-hailo8
```

For Hailo 8L hardware with Hailo 8L-compiled models, run:

```bash
./scripts/run.sh 03-full-onnx-hailo8l
```

For Hailo 10, run:

```bash
./scripts/run.sh 04-full-onnx-hailo10
```

Keep the terminal running. Open the PEK browser UI from your normal computer:

```text
http://raspberrypi.local:9999
```

If that does not work, use the Pi IP address:

```text
http://<raspberry-pi-ip-address>:9999
```

In the **AI Models** panel, enable one model first.

## If Hailo Models Fail

Confirm that the Hailo device is visible on the Raspberry Pi before starting
the PEK container:

```bash
ls /dev/hailo*
hailortcli fw-control identify
```

If the host cannot see the Hailo device, PEK inside the container will not be
able to use it.

[Back to README](index.md)
