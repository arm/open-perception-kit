# .devcontainer/Dockerfile
#FROM --platform=linux/arm64 debian:bookworm-slim
FROM --platform=linux debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PYTHONDONTWRITEBYTECODE=1

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# Base deps (build tools, GStreamer stack, Python, QoL tools)
RUN set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
    ca-certificates curl wget git sudo bash-completion mc unzip vim nano \
    build-essential meson ninja-build pkg-config gdb \
    python3 python3-venv python3-gi python3-gst-1.0 \
    libssl-dev libffi-dev zlib1g-dev libbz2-dev liblzma-dev libsqlite3-dev \
    libgstreamer1.0-dev gstreamer1.0-tools \
    libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-base gstreamer1.0-x gstreamer1.0-gl \
    libgstreamer-plugins-bad1.0-dev  gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly gstreamer1.0-libav \
    libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev \
    libwayland-dev libva-dev libv4l-dev libgtk-3-0; \
  update-ca-certificates; \
  rm -rf /var/lib/apt/lists/*

# onnx runtime

RUN wget https://github.com/microsoft/onnxruntime/releases/download/v1.18.1/onnxruntime-linux-aarch64-1.18.1.tgz
RUN tar xf onnxruntime-linux-aarch64-1.18.1.tgz
RUN sudo cp -r onnxruntime-linux-aarch64-1.18.1/include/* /usr/local/include/
RUN sudo cp -r onnxruntime-linux-aarch64-1.18.1/lib/* /usr/local/lib/
RUN sudo ldconfig

# uv (Python package manager)
RUN set -eux; \
  curl -LsSf https://astral.sh/uv/install.sh | \
    env UV_INSTALL_DIR=/usr/local/bin UV_NO_MODIFY_PATH=1 sh; \
  uv --version

# Non-root user
ARG USERNAME=devgoblin
ARG USER_UID=1000
ARG USER_GID=1000
RUN set -eux; \
  getent group "${USER_GID}" >/dev/null || groupadd --gid "${USER_GID}" "${USERNAME}"; \
  id -u "${USERNAME}" >/dev/null 2>&1 || useradd -m -u "${USER_UID}" -g "${USER_GID}" -s /bin/bash "${USERNAME}"; \
  usermod -aG video,render "${USERNAME}" || true; \
  mkdir -p /etc/sudoers.d; \
  echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/90-${USERNAME}"; \
  chmod 0440 "/etc/sudoers.d/90-${USERNAME}"; \
  mkdir -p /work && chown -R "${USER_UID}:${USER_GID}" /work

USER ${USERNAME}
WORKDIR /work

# Project-friendly defaults
ENV GST_DEBUG=2 \
    GST_PLUGIN_PATH=/work/development/build

CMD ["sleep","infinity"]