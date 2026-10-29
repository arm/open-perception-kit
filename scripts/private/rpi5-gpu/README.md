# Experimental RPi5 GPU runtime

Build a prepared OPK development image for the
[GPU quickstart](../../../development/ops-mlwl/README.md#raspberry-pi-5-gpu-quickstart).
All driver and emulation changes stay inside Docker. The host kernel, Mesa
packages and boot configuration are unchanged; only `/dev/dri/renderD128` is
passed through when running the demo.

On an AArch64 build machine, first build the regular development image with
MLWL enabled, then add the RPi5 runtime:

```sh
docker build --target opk-dev --build-arg OPK_MLWL=enabled -t opk-dev-mlwl .
docker build -f Dockerfile.rpi5-gpu --build-arg OPK_DEV_IMAGE=opk-dev-mlwl -t opk-rpi5-gpu .
```

Set these values in the checkout's `.env`:

```dotenv
OPK_DEV_IMAGE=opk-rpi5-gpu
OPK_MLWL_RUNTIME=rpi5-gpu
```

Run `./scripts/quick_start.sh`. Inside the container, build OPK with
`./scripts/build.sh`, supply the VGF models, and run
`./scripts/run.sh mlwl-vgf-demo`. Open `http://<host>:9999/` and enable the overlays.
The normal preflight rejects incompatible devices before startup.

The image uses checksum-pinned Mesa 26.2.1 and the same pinned Arm Graph/Tensor
sources as the CPU quickstart. Mesa enables deferred host operations and Int64
lowering. Graph uses Float32 in place of double, caps convolution dispatch work
and matches host/shader workgroups at 4×4×4. Source pins, patches and upstream
notices are retained under `/opt/opk-rpi5/*/share/doc/`.

This is an evaluation runtime; Vulkan CTS has not been run. The work
cap was calibrated on RPi5 FP32 UltraFace/YOLO workloads; it cannot guarantee
that every model avoids GPU resets. The previous YOLO measurements showed small
coordinate differences from ONNX, although repeated GPU results were identical.
CTS, other models and other devices need separate validation.
