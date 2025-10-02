# ⚡ AMP Development Forge

---

### 📦 Container

The repo supports multiple development platforms. 
To get an usabe docker environment you should first run setup-container.sh and select the proper platform:

- Default container: works everywhere (hopefully) but no hardware acceleration
- PC hardware accelerated x64 Linux: this container can run on a Linux PC and video hw acceleration is tunneled into the container

This generates the Dockerfile and devcontainer.json.
Now you can use them as you do usually:

- Open devcontainer: code .
- Then: Reopen in container

## 🛠️ Build and test

Basic task can be done in the container terminal using the shell scripts:
```
- ./scripts/build-elements.sh debug - to build the elements using meson/ninja
- ./scripts/build-elements.sh release
- ./scripts/test-elements.sh onnx - play a video pipeline that contains an onnx yolov8 inference
- ./scripts/test-elements.sh onnxweb - same as above but with webrtc endpoint
```
## 🍎 MacOS Setup

To play videos being played inside the container, you need ffmpeg:

```
brew instll ffmpeg

ffplay -hide_banner -fflags nobuffer -flags low_delay -f mpegts udp://127.0.0.1:5000
 ```

This executes ffplay in endless mode, so the container can publish videos any time.

