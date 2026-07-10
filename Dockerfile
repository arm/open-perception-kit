######################################################################
########## Base container defaults: bare minimum to run PEK ##########
######################################################################
FROM debian:trixie-slim AS pek-base

ARG ONNXRUNTIME_VERSION=1.24.4

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8 \
  PIP_DISABLE_PIP_VERSION_CHECK=1 \
  PYTHONDONTWRITEBYTECODE=1

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# Show base info (helps reading logs)
RUN set -eux; uname -a; cat /etc/os-release; dpkg --print-architecture

# Always start with update
RUN set -eux; \
  apt-get update; \
  rm -rf /var/lib/apt/lists/*

# Minimal core tools (runtime + build)
RUN set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  ca-certificates curl wget sudo unzip gnupg \
  build-essential meson ninja-build pkg-config cmake \
  libssl-dev libfmt-dev libfftw3-dev libsoup-3.0-dev libjson-glib-dev libcairo2-dev zip python3 python3-pip; \
  rm -rf /var/lib/apt/lists/*

# LLDB 17 for Colima
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        lldb-17 && \
    rm -rf /var/lib/apt/lists/*

RUN ln -sf /usr/bin/lldb-17 /usr/local/bin/lldb && \
    ln -sf /usr/bin/lldb-server-17 /usr/local/bin/lldb-server
				  	  			
# GStreamer core + base
RUN set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  libgstreamer1.0-dev gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-gl \
  libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-base \
  libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly  \
  gstreamer1.0-nice gstreamer1.0-pipewire; \
  rm -rf /var/lib/apt/lists/*

# Profiling tools
RUN set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  valgrind; \
  rm -rf /var/lib/apt/lists/*

# Clean apt cache
RUN set -eux; update-ca-certificates || true

# Install ONNX Runtime into image layers for reproducible builds and SBOM visibility.
RUN set -eux; \
  arch="$(uname -m)"; \
  case "$arch" in \
  x86_64) ort_arch="x64" ;; \
  aarch64) ort_arch="aarch64" ;; \
  *) echo "Unsupported architecture for ONNX Runtime: $arch" >&2; exit 1 ;; \
  esac; \
  ort_dir="onnxruntime-linux-${ort_arch}-${ONNXRUNTIME_VERSION}"; \
  ort_tgz="${ort_dir}.tgz"; \
  ort_url="https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/${ort_tgz}"; \
  tmp_dir="$(mktemp -d)"; \
  curl --fail --show-error --location --retry 5 --retry-delay 5 --retry-all-errors --output "$tmp_dir/$ort_tgz" "$ort_url"; \
  tar -xzf "$tmp_dir/$ort_tgz" -C "$tmp_dir"; \
  mkdir -p /opt/pek-deps/onnxruntime; \
  cp -r "$tmp_dir/$ort_dir/include" /opt/pek-deps/onnxruntime/; \
  cp -r "$tmp_dir/$ort_dir/lib" /opt/pek-deps/onnxruntime/; \
  rm -rf "$tmp_dir"

EXPOSE 8000
EXPOSE 8001
EXPOSE 9999
EXPOSE 8080
EXPOSE 2222

# Non-root user
ARG USERNAME=devgoblin
ARG USER_UID=1000
ARG USER_GID=1000
RUN set -eux; \
  getent group "${USER_GID}" >/dev/null || groupadd --gid "${USER_GID}" "${USERNAME}"; \
  id -u "${USERNAME}" >/dev/null 2>&1 || useradd -m -u "${USER_UID}" -g "${USER_GID}" -s /bin/bash "${USERNAME}"; \
  getent group video >/dev/null 2>&1 || groupadd video; \
  getent group render >/dev/null 2>&1 || groupadd render; \
  getent group audio >/dev/null 2>&1 || groupadd audio; \
  usermod -aG video,audio,render "${USERNAME}"; \
  mkdir -p /etc/sudoers.d; \
  echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/90-${USERNAME}"; \
  chmod 0440 "/etc/sudoers.d/90-${USERNAME}"; \
  mkdir -p /work && chown -R "${USER_UID}:${USER_GID}" /work; \
  test -p /tmp/pekcomm || mkfifo --mode=640 /tmp/pekcomm && \
  chown ${USERNAME} /tmp/pekcomm

USER ${USERNAME}
WORKDIR /work

# Project-friendly defaults
ENV GST_DEBUG=2 \
  GST_PLUGIN_PATH=/work/development/build/meson-out

ENV LD_LIBRARY_PATH=""
ENV LD_LIBRARY_PATH=/opt/pek-deps/onnxruntime/lib

######################################################################
################# Minimal container with docs and CI #################
######################################################################
FROM pek-base AS pek-docs-base

ARG USERNAME=devgoblin
ARG ACTIONLINT_VERSION=1.7.12

USER root

# Dev / CI tools required for docs and quality checks
RUN set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  git shfmt clang-format ssh \
  openjdk-25-jdk graphviz pandoc pre-commit doxygen \
  python3-dev python3-venv python3-gi python3-gst-1.0 \
  libffi-dev zlib1g-dev libbz2-dev liblzma-dev libsqlite3-dev v4l-utils; \
  rm -rf /var/lib/apt/lists/*

# Map only Debian architectures backed by official actionlint Linux release assets.
RUN set -eux; \
  arch="$(dpkg --print-architecture)"; \
  case "${arch}" in \
    amd64) actionlint_arch="amd64" ;; \
    i386) actionlint_arch="386" ;; \
    arm64) actionlint_arch="arm64" ;; \
    armel|armhf) actionlint_arch="armv6" ;; \
    *) echo "Unsupported actionlint architecture: ${arch}" >&2; exit 1 ;; \
  esac; \
  actionlint_archive="actionlint_${ACTIONLINT_VERSION}_linux_${actionlint_arch}.tar.gz"; \
  actionlint_base_url="https://github.com/rhysd/actionlint/releases/download/v${ACTIONLINT_VERSION}"; \
  tmp_dir="$(mktemp -d)"; \
  curl -fsSLo "${tmp_dir}/${actionlint_archive}" "${actionlint_base_url}/${actionlint_archive}"; \
  curl -fsSLo "${tmp_dir}/checksums.txt" "${actionlint_base_url}/actionlint_${ACTIONLINT_VERSION}_checksums.txt"; \
  cd "${tmp_dir}"; \
  grep " ${actionlint_archive}$" checksums.txt | sha256sum -c -; \
  tar -xzf "${actionlint_archive}" actionlint; \
  install -m 0755 actionlint /usr/local/bin/actionlint; \
  cd /; \
  rm -rf "${tmp_dir}"; \
  actionlint -version

# uv (Python package manager) for dev/CI tooling
RUN set -eux; \
  curl --proto "=https" -LsSf https://astral.sh/uv/install.sh | \
  env UV_INSTALL_DIR=/usr/local/bin UV_NO_MODIFY_PATH=1 sh; \
  uv --version

# Install PlantUML JAR into image layers for docs generation and SBOM visibility.
ARG PLANTUML_VERSION=1.2026.2
RUN set -eux; \
  mkdir -p /opt/pek-deps; \
  wget --secure-protocol=TLSv1_2 \
    "https://github.com/plantuml/plantuml/releases/download/v${PLANTUML_VERSION}/plantuml-mit-${PLANTUML_VERSION}.jar" \
    -O "/opt/pek-deps/plantuml-mit-${PLANTUML_VERSION}.jar"

USER ${USERNAME}
WORKDIR /work


######################################################################
#################### PC Base Development Container ###################
######################################################################
FROM pek-docs-base AS pek-dev-base

ARG USERNAME=devgoblin

USER root

# Extra QoL and debugging tools for development shells
RUN set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  locales bash-completion mc vim nano gdb clangd net-tools zsh \
  openssh-client less ripgrep fd-find tmux; \
  rm -rf /var/lib/apt/lists/*

# libav can sometimes be the troublemaker; probe then install
RUN set -eux; \
  apt-get update; \
  if apt-get install -y --no-install-recommends --dry-run gstreamer1.0-libav; then \
  apt-get install -y --no-install-recommends gstreamer1.0-libav; \
  else \
  echo 'NOTE: gstreamer1.0-libav not available on this image/mirror'; \
  fi; \
  rm -rf /var/lib/apt/lists/*

# Install Firefox for Perception Experience Kit's web-based UI and testing in case docker port forwarding fails.
RUN set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  firefox-esr; \
  rm -rf /var/lib/apt/lists/*

# Install Python dev tool dependencies into an image-owned virtual environment.
COPY tools/expkits-ci /tmp/pek-tools/expkits-ci
COPY tools/plumber /tmp/pek-tools/plumber
RUN set -eux; \
  uv venv --system-site-packages /opt/pek-venvs/devtools; \
  uv pip install --python /opt/pek-venvs/devtools/bin/python \
  /tmp/pek-tools/expkits-ci \
  /tmp/pek-tools/plumber; \
  rm -rf /tmp/pek-tools

ENV PEK_DEVTOOLS_VENV=/opt/pek-venvs/devtools

USER ${USERNAME}
WORKDIR /work

######################################################################
###################### RPI5 Development Container ####################
######################################################################
FROM pek-dev-base AS pek-dev-rpi5-h8
# The base stage switches to a non-root user; return to root for apt/system changes.
ARG USERNAME=devgoblin

USER root
# Add Raspberry Pi repository
RUN set -eux; \
  apt-get update; \
  # TODO: use key
  echo "deb [arch=arm64 trusted=yes] https://archive.raspberrypi.com/debian trixie main" \
  > /etc/apt/sources.list.d/raspberrypi.list

# Camera and graphics libraries
RUN set -eux; \
  apt-get update && apt-get install -y --no-install-recommends \
  libv4l-dev libgl1-mesa-dri libglx-mesa0 libegl1 libgbm1 libdrm2 mesa-utils libdrm-dev libgbm-dev \
  libcamera-tools libcamera-dev libcamera-ipa libcamera-v4l2 rpicam-apps \
  alsa-utils gstreamer1.0-libcamera gstreamer1.0-alsa; \
  rm -rf /var/lib/apt/lists/*

RUN set -eux; \
  apt-get update && apt-get install -y --no-install-recommends \
  hailo-models hailo-tappas-core hailort \
  python3-hailo-tappas python3-hailort rpicam-apps-hailo-postprocess; \
  rm -rf /var/lib/apt/lists/*

USER ${USERNAME}
WORKDIR /work

######################################################################
################### RPI5 Development Container (H10) #################
######################################################################
FROM pek-dev-base AS pek-dev-rpi5-h10
# The base stage switches to a non-root user; return to root for apt/system changes.
ARG USERNAME=devgoblin

USER root
# Add Raspberry Pi repository
RUN set -eux; \
  apt-get update; \
  # TODO: use key
  echo "deb [arch=arm64 trusted=yes] https://archive.raspberrypi.com/debian trixie main" \
  > /etc/apt/sources.list.d/raspberrypi.list

# Camera and graphics libraries
RUN set -eux; \
  apt-get update && apt-get install -y --no-install-recommends \
  libv4l-dev libgl1-mesa-dri libglx-mesa0 libegl1 libgbm1 libdrm2 mesa-utils libdrm-dev libgbm-dev \
  libcamera-tools libcamera-dev libcamera-ipa libcamera-v4l2 rpicam-apps \
  alsa-utils gstreamer1.0-libcamera gstreamer1.0-alsa; \
  rm -rf /var/lib/apt/lists/*

# Hailo H10 user-space stack only.
# Kernel driver packages (DKMS / h10-hailort-pcie-driver) are host-level and
# fail in container builds because they require host kernel/module tooling.
RUN set -eux; \
  apt-get update && apt-get install -y --no-install-recommends \
  h10-hailort python3-h10-hailort \
  hailo-tappas-core python3-hailo-tappas \
  hailo-models rpicam-apps-hailo-postprocess; \
  rm -rf /var/lib/apt/lists/*

USER ${USERNAME}
WORKDIR /work

######################################################################
###################### Deployment container ##########################
######################################################################
FROM pek-docs-base AS pek-deployment-base

ARG USERNAME=devgoblin

USER root
ARG PEK_PIPELINE=onnx

# Copy project into image for self-contained deployment
COPY --chown=${USERNAME}:${USERNAME} . /work

ENV PEK_PIPELINE=${PEK_PIPELINE}

USER ${USERNAME}
WORKDIR /work

ENTRYPOINT ["/work/scripts/private/deployment-process.sh"]

######################################################################
###################### Deployment container ##########################
######################################################################
FROM pek-dev-base AS pek-dev-sonar

USER root

ENV SONAR_SCANNER_VERSION="8.0.1.6346"

ENV SONAR_HOST_URL="https://sonarqube.mobilestudio.aws.arm.com" \
    PATH=/opt/sonar/sonar-scanner-${SONAR_SCANNER_VERSION}/bin:${PATH}

RUN set -eux; \
    apt-get update; apt-get install -y --no-install-recommends gcovr; \
    rm -rf /var/lib/apt/lists/*; \
    mkdir -p /opt/sonar; \
    curl --proto "=https" -fsSLo /tmp/sonar-scanner.zip \
        "https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-${SONAR_SCANNER_VERSION}.zip"; \
    unzip -o /tmp/sonar-scanner.zip -d /opt/sonar/; \
    rm -f /tmp/sonar-scanner.zip

######################################################################
################ Rich development environment container ##############
######################################################################
FROM pek-dev-sonar AS pek-dev-rich

ARG USERNAME=devgoblin
USER root

# ---- Basic packages for development ----
RUN apt-get update && \
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  xz-utils powerline fonts-powerline eza bat clangd gosu \
  lua5.1 luarocks tree-sitter-cli wl-clipboard \
  iproute2 iputils-ping traceroute iputils-arping dnsutils tcpdump nmap; \
  rm -rf /var/lib/apt/lists/*

RUN luarocks install jsregexp

RUN chsh -s /usr/bin/zsh ${USERNAME}

COPY .devcontainer/uidgid-entrypoint.sh /usr/local/bin/uidgid-entrypoint
RUN chmod +x /usr/local/bin/uidgid-entrypoint

# ---- Locale ----
RUN sed -i 's/^# *\(en_US.UTF-8 UTF-8\)/\1/' /etc/locale.gen && \
  locale-gen en_US.UTF-8 && update-locale LANG=en_US.UTF-8
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# ---- Install Neovim v0.11.5 via AppImage ----
ARG NVIM_VERSION=v0.12.1
ARG NVIM_APPIMAGE=nvim-linux-x86_64.appimage

RUN touch /container_env

RUN curl --proto "=https" -LO https://github.com/neovim/neovim/releases/download/${NVIM_VERSION}/${NVIM_APPIMAGE} && \
  chmod +x ${NVIM_APPIMAGE} && \
  ./${NVIM_APPIMAGE} --appimage-extract && \
  mv squashfs-root /opt/nvim && \
  ln -s /opt/nvim/usr/bin/nvim /usr/local/bin/nvim && \
  rm ${NVIM_APPIMAGE}

RUN update-alternatives --install /usr/bin/vi vi /usr/local/bin/nvim 60 && \
  update-alternatives --install /usr/bin/vim vim /usr/local/bin/nvim 60 && \
  update-alternatives --set vim /usr/local/bin/nvim && \
  update-alternatives --set vi /usr/local/bin/nvim 

ARG CPP_TOOLS_VERSION=v1.29.3
ARG CPP_TOOLS_APPIMAGE=cpptools-linux-x64.vsix
RUN curl --proto "=https" -LO https://github.com/microsoft/vscode-cpptools/releases/download/${CPP_TOOLS_VERSION}/${CPP_TOOLS_APPIMAGE} && \
  mkdir -p /home/${USERNAME}/bin/cpptools && \
  unzip ${CPP_TOOLS_APPIMAGE} -d /home/${USERNAME}/bin/cpptools && \
  chmod +x /home/${USERNAME}/bin/cpptools/extension/debugAdapters/bin/OpenDebugAD7 && \
  ln -s /home/${USERNAME}/bin/cpptools/extension/debugAdapters/bin/OpenDebugAD7 /usr/local/bin/OpenDebugAD7

# ---- Install oh-my-zsh for dev user ----
RUN curl --proto "=https" -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh -o /tmp/install-ohmyzsh.sh && \
  chmod +x /tmp/install-ohmyzsh.sh && \
  su - ${USERNAME} -c "env RUNZSH=no CHSH=no KEEP_ZSHRC=yes /tmp/install-ohmyzsh.sh" && \
  rm /tmp/install-ohmyzsh.sh

# ---- Symlinks to mounted configs ----
# We expect /home/dev/configs to be provided via a bind-mount at runtime.
RUN mkdir -p /home/${USERNAME}/.config && \
  ln -sfn /home/${USERNAME}/configs/zshrc /home/${USERNAME}/.zshrc && \
  ln -sfn /home/${USERNAME}/configs/nvchad_2026_04 /home/${USERNAME}/.config/nvim && \
  chown -R ${USER_UID}:${USER_GID} /home/${USERNAME}/.config /home/${USERNAME}/.zshrc

# ---- SSH agent socket mapping ----
ENV SSH_AUTH_SOCK=/ssh-agent
ENV SHELL=/bin/zsh

ENTRYPOINT ["/usr/local/bin/uidgid-entrypoint"]

USER ${USERNAME}
