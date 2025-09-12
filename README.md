# ⚡ AMP Development Forge

---

### 📦 Container

The repo containes a Dockerfile and a devcontainer.json works as usual.

- Open devcontainer: code .
- Then: Terminal → New Terminal

---

## 🛠️ Build

Basic task can be done using the command.sh script:

- ./development/command.sh clean
- ./development/command.sh build
- ./development/command.sh test

---

## 🍎 MacOS Setup

brew instll ffmpeg
ffplay -fflags nobuffer -flags low_delay -f mpegts udp://127.0.0.1:5000


### On host

defaults write org.xquartz.X11 enable_iglx -bool true
open -a XQuartz
xhost +127.0.0.1

### In-Container

export DISPLAY=host.docker.internal:0
export GST_XIMAGESINK_DISABLE_SHM=1
export LIBGL_ALWAYS_INDIRECT=1