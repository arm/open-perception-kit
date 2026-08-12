# syntax=docker/dockerfile:1

ARG BUILDPLATFORM
ARG TARGETPLATFORM
ARG TARGETARCH

# ==============================================================================
# External Base Images
# ==============================================================================

# ==============================================================================
# Development Tooling Images
# ==============================================================================

FROM --platform=${BUILDPLATFORM} debian:trixie-slim AS pek-build-base

ARG ONNXRUNTIME_VERSION=1.24.4

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8 \
  PIP_DISABLE_PIP_VERSION_CHECK=1 \
  PYTHONDONTWRITEBYTECODE=1 \
  LD_LIBRARY_PATH=/opt/pek-deps/onnxruntime/lib

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

COPY tools/perception/sdk.json /tmp/perception-sdk.json
COPY --chmod=0755 scripts/private/install-perception-flatbuffers.sh /usr/local/bin/install-perception-flatbuffers

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  build-essential \
  ca-certificates \
  cmake \
  curl \
  git \
  libcairo2-dev \
  libfftw3-dev \
  libfmt-dev \
  libgstreamer-plugins-bad1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  libgstreamer1.0-dev \
  libjson-glib-dev \
  libsoup-3.0-dev \
  libssl-dev \
  meson \
  ninja-build \
  pkg-config \
  python3 \
  python3-dev \
  unzip; \
  update-ca-certificates; \
  install-perception-flatbuffers /tmp/perception-sdk.json; \
  rm -f /tmp/perception-sdk.json


FROM pek-build-base AS pek-cross-build-base

# Downloadable demo media is isolated so development and deployment images can
# reuse it without coupling it to source or tool layers.
FROM --platform=${BUILDPLATFORM} debian:trixie-slim AS pek-demo-media

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends bash ca-certificates curl; \
  update-ca-certificates

ARG NO_EXAMPLE_CONTENT=false

WORKDIR /work
COPY --chmod=0755 scripts/download-data.sh scripts/download-data.sh
RUN if [ "${NO_EXAMPLE_CONTENT}" != "true" ]; then \
      ./scripts/download-data.sh; \
    else \
      mkdir -p data/videos; \
    fi

# Model artifacts are resolved in a dedicated stage so Hugging Face tokens stay
# scoped to build-time model download.
FROM --platform=${BUILDPLATFORM} python:3.13-slim-trixie AS pek-models

ENV PIP_DISABLE_PIP_VERSION_CHECK=1 \
  PYTHONDONTWRITEBYTECODE=1

RUN python3 -m pip install --no-cache-dir \
  huggingface_hub==1.18.0 \
  jsonschema==4.26.0

WORKDIR /work
COPY config config
COPY --chmod=0755 scripts/download-models.py scripts/download-models.py
ARG HF_DOWNLOAD_CACHEBUST
RUN --mount=type=cache,target=/root/.cache/huggingface \
  --mount=type=secret,id=huggingface_token,env=HF_TOKEN \
  HF_DOWNLOAD_CACHEBUST="${HF_DOWNLOAD_CACHEBUST}" \
  ./scripts/download-models.py --models-dir config/models --token "${HF_TOKEN:-}"

# Development base extends the shared native build tooling. PEK source and build
# outputs come from the mounted checkout, not from this image.
FROM pek-build-base AS pek-dev-base

ARG ONNXRUNTIME_VERSION
ARG NPM_FALLBACK_REGISTRY=https://artifactory.arm.com:443/artifactory/api/npm/mirrors.npmjs_org
ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=1000

COPY tools/perception/sdk.json /tmp/perception-sdk.json
COPY development/web/package-lock.json /tmp/pek-web-package-lock.json

RUN set -eux; uname -a; cat /etc/os-release; dpkg --print-architecture

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  file gnupg gstreamer1.0-gl gstreamer1.0-nice gstreamer1.0-pipewire \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly \
  gstreamer1.0-tools gstreamer1.0-x lldb-17 nodejs npm pre-commit python3-gi python3-pip python3-venv \
  shellcheck shfmt sudo valgrind wget zip; \
  update-ca-certificates

RUN set -eux; \
  esbuild_url="$(node -e 'const lock=require("/tmp/pek-web-package-lock.json"); console.log(lock.packages["node_modules/esbuild-wasm"].resolved)')"; \
  flatbuffers_url="$(node -e 'const config=require("/tmp/perception-sdk.json"); console.log(config.typescript_build.flatbuffers_runtime.url)')"; \
  typescript_url="$(node -e 'const config=require("/tmp/perception-sdk.json"); console.log(config.typescript_build.typescript.url)')"; \
  npm_args=(--global --ignore-scripts --no-audit --no-fund); \
  if ! timeout 180s env npm_config_fetch_retries=1 npm install "${npm_args[@]}" \
      "${esbuild_url}" "${flatbuffers_url}" "${typescript_url}"; then \
    esbuild_url="${NPM_FALLBACK_REGISTRY}/${esbuild_url#https://registry.npmjs.org/}"; \
    flatbuffers_url="${NPM_FALLBACK_REGISTRY}/${flatbuffers_url#https://registry.npmjs.org/}"; \
    typescript_url="${NPM_FALLBACK_REGISTRY}/${typescript_url#https://registry.npmjs.org/}"; \
    env npm_config_fetch_retries=3 npm install "${npm_args[@]}" \
      "${esbuild_url}" "${flatbuffers_url}" "${typescript_url}"; \
  fi; \
  rm -f /tmp/pek-web-package-lock.json

RUN ln -sf /usr/bin/lldb-17 /usr/local/bin/lldb && \
  ln -sf /usr/bin/lldb-server-17 /usr/local/bin/lldb-server

ARG ACTIONLINT_VERSION=1.7.12

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
  curl --location -fsSLo "${tmp_dir}/${actionlint_archive}" "${actionlint_base_url}/${actionlint_archive}"; \
  curl --location -fsSLo "${tmp_dir}/checksums.txt" "${actionlint_base_url}/actionlint_${ACTIONLINT_VERSION}_checksums.txt"; \
  cd "${tmp_dir}"; \
  grep " ${actionlint_archive}$" checksums.txt | sha256sum -c -; \
  tar -xzf "${actionlint_archive}" actionlint; \
  install -m 0755 actionlint /usr/local/bin/actionlint; \
  cd /; \
  rm -rf "${tmp_dir}"; \
  actionlint -version; \
  shellcheck --version

COPY --chmod=0755 scripts/private/install-onnxruntime.sh /usr/local/bin/install-onnxruntime
RUN install-onnxruntime "${ONNXRUNTIME_VERSION}"

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
  mkdir -p /work; \
  chown -R "${USER_UID}:${USER_GID}" /work; \
  test -p /tmp/pekcomm || mkfifo --mode=640 /tmp/pekcomm; \
  chown "${USERNAME}" /tmp/pekcomm

RUN set -eux; \
  curl --proto "=https" -LsSf https://astral.sh/uv/install.sh | \
  env UV_INSTALL_DIR=/usr/local/bin UV_NO_MODIFY_PATH=1 sh; \
  uv --version

COPY tools/expkits-ci /tmp/pek-tools/expkits-ci
COPY tools/plumber /tmp/pek-tools/plumber
COPY generated/perception/python /tmp/pek-tools/perception
RUN set -eux; \
  uv pip install --system --break-system-packages jsonschema==4.26.0; \
  flatbuffers_wheel="$(python3 -c 'import json; wheel=json.load(open("/tmp/perception-sdk.json"))["flatbuffers"]["python_wheel"]; print(wheel["url"] + "#sha256=" + wheel["sha256"])')"; \
  uv venv --system-site-packages /opt/pek-venvs/devtools; \
  uv pip install --python /opt/pek-venvs/devtools/bin/python \
  /tmp/pek-tools/expkits-ci \
  /tmp/pek-tools/perception \
  /tmp/pek-tools/plumber \
  huggingface_hub==1.18.0 \
  "${flatbuffers_wheel}"; \
  cd /tmp; \
  /opt/pek-venvs/devtools/bin/python -c 'import perception, plumber'; \
  chown -R "${USER_UID}:${USER_GID}" /opt/pek-venvs/devtools; \
  rm -rf /tmp/pek-tools /tmp/perception-sdk.json

EXPOSE 8000 8001 9999 8080 2222

ENV GST_DEBUG=2 \
  GST_PLUGIN_PATH=/work/development/build/meson-out \
  LD_LIBRARY_PATH=/opt/pek-deps/onnxruntime/lib:/work/development/build/meson-out \
  PEK_HAILORT=disabled \
  PEK_DEVTOOLS_VENV=/opt/pek-venvs/devtools \
  PATH=/opt/pek-venvs/devtools/bin:${PATH}

USER ${USERNAME}
WORKDIR /work

# Developer shell, editor, debugger, and network tooling.
FROM pek-dev-base AS pek-dev-tools

ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=1000
ARG NVIM_VERSION=v0.12.1
ARG CPP_TOOLS_VERSION=v1.29.3
ARG TARGETARCH
ARG EXECUTORCH_VERSION=1.3.1
ARG EXECUTORCH_DEB_REVISION=1
ARG EXECUTORCH_ARTIFACTORY_SERVER=https://artifactory.arm.com:443
ARG EXECUTORCH_ARTIFACTORY_REPOSITORY=ai-expkits-internal.opk-deb
ARG EXECUTORCH_ARTIFACTORY_DISTRIBUTION=trixie
ARG EXECUTORCH_ARTIFACTORY_COMPONENT=main
ARG EXECUTORCH_ARTIFACTORY_USERNAME=""
ARG EXECUTORCH_ARTIFACTORY_PASSWORD=""

USER root

RUN --mount=type=bind,source=var,target=/tmp/pek-executorch-packages,ro \
    --mount=type=bind,source=scripts/private/executorch/install-executorch-deb.sh,target=/tmp/install-executorch-deb.sh,ro \
  bash /tmp/install-executorch-deb.sh

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  bash-completion bat clangd dnsutils eza fd-find firefox-esr fonts-powerline \
  gdb gosu iproute2 iputils-arping iputils-ping less locales lua5.1 \
  luarocks mc nano neovim net-tools nmap openssh-client powerline ripgrep \
  tcpdump tmux traceroute tree-sitter-cli v4l-utils vim wl-clipboard \
  xz-utils zsh; \
  if apt-get install -y --no-install-recommends --dry-run gstreamer1.0-libav; then \
    apt-get install -y --no-install-recommends gstreamer1.0-libav; \
  else \
    echo 'NOTE: gstreamer1.0-libav not available on this image/mirror'; \
  fi; \
  sed -i 's/^# *\(en_US.UTF-8 UTF-8\)/\1/' /etc/locale.gen; \
  locale-gen en_US.UTF-8; \
  update-locale LANG=en_US.UTF-8; \
  chsh -s /usr/bin/zsh "${USERNAME}"

RUN set -eux; \
  case "${TARGETARCH}" in \
    amd64) nvim_arch=x86_64; cpptools_arch=x64 ;; \
    arm64) nvim_arch=arm64; cpptools_arch=arm64 ;; \
    *) echo "Unsupported development-tools architecture: ${TARGETARCH}" >&2; exit 1 ;; \
  esac; \
  nvim_archive="nvim-linux-${nvim_arch}.tar.gz"; \
  curl --proto "=https" -fsSLo "/tmp/${nvim_archive}" \
  "https://github.com/neovim/neovim/releases/download/${NVIM_VERSION}/${nvim_archive}"; \
  mkdir -p /opt/nvim; \
  tar -xzf "/tmp/${nvim_archive}" --strip-components=1 -C /opt/nvim; \
  cpptools_archive="cpptools-linux-${cpptools_arch}.vsix"; \
  curl --proto "=https" -fsSLo "/tmp/${cpptools_archive}" \
  "https://github.com/microsoft/vscode-cpptools/releases/download/${CPP_TOOLS_VERSION}/${cpptools_archive}"; \
  mkdir -p "/home/${USERNAME}/bin/cpptools"; \
  unzip -q "/tmp/${cpptools_archive}" -d "/home/${USERNAME}/bin/cpptools"; \
  chown -R "${USER_UID}:${USER_GID}" "/home/${USERNAME}/bin"; \
  rm -f "/tmp/${nvim_archive}" "/tmp/${cpptools_archive}"

RUN set -eux; \
  luarocks install jsregexp; \
  ln -sf /opt/nvim/bin/nvim /usr/local/bin/nvim; \
  update-alternatives --install /usr/bin/vi vi /usr/local/bin/nvim 60; \
  update-alternatives --install /usr/bin/vim vim /usr/local/bin/nvim 60; \
  update-alternatives --set vim /usr/local/bin/nvim; \
  update-alternatives --set vi /usr/local/bin/nvim; \
  chmod +x "/home/${USERNAME}/bin/cpptools/extension/debugAdapters/bin/OpenDebugAD7"; \
  ln -sf "/home/${USERNAME}/bin/cpptools/extension/debugAdapters/bin/OpenDebugAD7" /usr/local/bin/OpenDebugAD7; \
  curl --proto "=https" -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh \
  -o /tmp/install-ohmyzsh.sh; \
  chmod +x /tmp/install-ohmyzsh.sh; \
  su - "${USERNAME}" -c "env RUNZSH=no CHSH=no KEEP_ZSHRC=yes /tmp/install-ohmyzsh.sh"; \
  rm -f /tmp/install-ohmyzsh.sh; \
  mkdir -p "/home/${USERNAME}/.config" "/home/${USERNAME}/configs"; \
  ln -sfn "/home/${USERNAME}/configs/zshrc" "/home/${USERNAME}/.zshrc"; \
  ln -sfn "/home/${USERNAME}/configs/nvchad_2026_04" "/home/${USERNAME}/.config/nvim"; \
  chown -R "${USER_UID}:${USER_GID}" "/home/${USERNAME}/.config" "/home/${USERNAME}/configs" "/home/${USERNAME}/.zshrc"

COPY --chmod=0444 .devcontainer/configs/zshrc /home/${USERNAME}/configs/zshrc

COPY --chmod=0755 scripts/private/development-entrypoint.sh /usr/local/bin/development-entrypoint

ENV LANG=en_US.UTF-8 \
  LC_ALL=en_US.UTF-8 \
  SHELL=/bin/zsh \
  SSH_AUTH_SOCK=/ssh-agent

ENTRYPOINT ["/usr/local/bin/development-entrypoint"]

# Final devcontainer image. Contract: tools and dependency libraries only. The
# repository is mounted at /work; PEK binaries are built from that checkout.
FROM pek-dev-tools AS pek-dev

ARG USERNAME=dev
ARG ONNXRUNTIME_VERSION
ARG PEK_PICAMERA=disabled

USER root

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  if [ "${PEK_PICAMERA}" = enabled ]; then \
    test "$(dpkg --print-architecture)" = arm64; \
    echo "deb [arch=arm64 trusted=yes] https://archive.raspberrypi.com/debian trixie main" \
    > /etc/apt/sources.list.d/raspberrypi.list; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
    gstreamer1.0-libcamera libcamera-ipa; \
  fi

RUN set -eux; \
  rm -rf /opt/pek-deps/onnxruntime-arm64; \
  if [ "$(dpkg --print-architecture)" = arm64 ]; then \
    ln -s onnxruntime /opt/pek-deps/onnxruntime-arm64; \
  else \
    install-onnxruntime "${ONNXRUNTIME_VERSION}" arm64 /opt/pek-deps/onnxruntime-arm64; \
  fi

USER ${USERNAME}
WORKDIR /work
COPY --from=pek-demo-media \
  /work/data/videos /opt/pek-app/data/videos
COPY --from=pek-models \
  /work/config/models /opt/pek-app/config/models

# ==============================================================================
# Documentation Image Lane
# ==============================================================================

FROM pek-dev-base AS pek-docs

ARG USERNAME=dev
ARG PLANTUML_VERSION=1.2026.2

USER root

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  doxygen graphviz openjdk-25-jdk pandoc

RUN set -eux; \
  mkdir -p /opt/pek-deps; \
  plantuml_jar="plantuml-mit-${PLANTUML_VERSION}.jar"; \
  plantuml_base_url="https://github.com/plantuml/plantuml/releases/download/v${PLANTUML_VERSION}"; \
  curl --location -fsSLo "/opt/pek-deps/${plantuml_jar}" "${plantuml_base_url}/${plantuml_jar}"

USER ${USERNAME}
WORKDIR /work

# ==============================================================================
# CI Image Lane
# ==============================================================================

FROM pek-dev-base AS pek-ci

ARG USERNAME=dev
ARG PLANTUML_VERSION=1.2026.2
ARG SONAR_SCANNER_VERSION=8.0.1.6346

USER root

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  ccache doxygen gcovr graphviz libbz2-dev libffi-dev liblzma-dev libsqlite3-dev \
  openjdk-25-jdk pandoc python3-dev python3-gi python3-gst-1.0 \
  python3-venv zlib1g-dev

RUN set -eux; \
  mkdir -p /opt/pek-deps; \
  plantuml_jar="plantuml-mit-${PLANTUML_VERSION}.jar"; \
  plantuml_base_url="https://github.com/plantuml/plantuml/releases/download/v${PLANTUML_VERSION}"; \
  curl --location -fsSLo "/opt/pek-deps/${plantuml_jar}" "${plantuml_base_url}/${plantuml_jar}"

RUN set -eux; \
  mkdir -p /opt/sonar; \
  curl --proto "=https" -fsSLo /tmp/sonar-scanner.zip \
  "https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-${SONAR_SCANNER_VERSION}.zip"; \
  unzip -q /tmp/sonar-scanner.zip -d /opt/sonar; \
  rm -f /tmp/sonar-scanner.zip

ENV PATH=/opt/sonar/sonar-scanner-${SONAR_SCANNER_VERSION}/bin:${PATH}

USER ${USERNAME}
WORKDIR /work

# ==============================================================================
# Deployment Build and Runtime Images
# ==============================================================================

FROM debian:trixie-slim AS pek-gstreamer-runtime-base

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  ca-certificates \
  gstreamer1.0-plugins-base \
  gstreamer1.0-tools \
  libgstreamer1.0-0; \
  update-ca-certificates; \
  rm -rf /var/lib/apt/lists/*

FROM pek-cross-build-base AS pek-deployment-build

ARG TARGETARCH
ARG NO_EXAMPLE_CONTENT=false
ARG ONNXRUNTIME_VERSION

COPY --chmod=0755 scripts/private/install-target-sysroot.sh /usr/local/bin/install-target-sysroot
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  if [ "${TARGETARCH}" != arm64 ]; then \
    echo "Unsupported deployment architecture: ${TARGETARCH}. Expected arm64." >&2; \
    exit 1; \
  fi; \
  if [ "${TARGETARCH}" != "$(dpkg --print-architecture)" ]; then \
    install-target-sysroot "${TARGETARCH}"; \
  fi

COPY --chmod=0755 scripts/private/install-onnxruntime.sh /usr/local/bin/install-onnxruntime
RUN install-onnxruntime \
  "${ONNXRUNTIME_VERSION}" "${TARGETARCH}" "/opt/pek-deps/onnxruntime-${TARGETARCH}"

WORKDIR /work
COPY development/meson.build development/meson.options development/
COPY development/subprojects/*.wrap development/subprojects/
COPY development/subprojects/packagefiles development/subprojects/packagefiles
RUN meson subprojects download --sourcedir /work/development

COPY scripts/build-elements.sh scripts/build-elements.sh
COPY scripts/private/shtools.sh scripts/private/shtools.sh
COPY scripts/private/deployment-runtime.sh scripts/private/deployment-runtime.sh
COPY development development
COPY generated generated
COPY --from=pek-models /work/config config
COPY data data
COPY --from=pek-demo-media /work/data/videos /work/data/videos

RUN set -eux; \
  native_arch="$(dpkg --print-architecture)"; \
  extra_setup_args=(); \
  if [ "${TARGETARCH}" != "${native_arch}" ]; then \
    extra_setup_args=("--extra-setup-args=--cross-file=/work/development/cross/aarch64-linux-gnu.ini"); \
  fi; \
  mkdir -p /work/tools; \
  PEK_HAILORT=disabled \
  PEK_ONNXRUNTIME_ROOT="/opt/pek-deps/onnxruntime-${TARGETARCH}" \
  NINJAFLAGS=-j2 \
  ./scripts/build-elements.sh release false "${extra_setup_args[@]}"; \
  mkdir -p /opt/pek-app/development/build/meson-out /opt/pek-app/tools /opt/pek-app/scripts/private; \
  find /work/development/build/meson-out -maxdepth 1 -type f -name "*.so" -exec cp {} /opt/pek-app/development/build/meson-out/ \; ; \
  cp /work/tools/pek-menu /opt/pek-app/tools/; \
  cp /work/scripts/private/deployment-runtime.sh /opt/pek-app/scripts/private/; \
  chmod +x /opt/pek-app/tools/pek-menu /opt/pek-app/scripts/private/deployment-runtime.sh; \
  cp -r /work/config /opt/pek-app/; \
  cp -r /work/data /opt/pek-app/; \
  cp -r /work/development/web /opt/pek-app/development/

FROM pek-gstreamer-runtime-base AS pek-deployment-base

ARG USERNAME=pek
ARG USER_UID=1000
ARG USER_GID=1000
ARG PEK_PIPELINE=yolov11-onnx
ARG PEK_PICAMERA=disabled

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8 \
  GST_DEBUG=2 \
  GST_PLUGIN_PATH=/work/development/build/meson-out \
  LD_LIBRARY_PATH=/opt/pek-deps/onnxruntime/lib:/work/development/build/meson-out \
  PEK_PIPELINE=${PEK_PIPELINE}

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  gstreamer1.0-nice \
  gstreamer1.0-pipewire \
  gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-good \
  libcairo2 \
  libfftw3-single3 \
  libfmt10 \
  libjson-glib-1.0-0 \
  libsoup-3.0-0 \
  libssl3t64; \
  if [ "${PEK_PICAMERA}" = enabled ]; then \
    test "$(dpkg --print-architecture)" = arm64; \
    echo "deb [arch=arm64 trusted=yes] https://archive.raspberrypi.com/debian trixie main" \
    > /etc/apt/sources.list.d/raspberrypi.list; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
    gstreamer1.0-libcamera libcamera-ipa; \
  fi; \
  install -m 0755 /usr/lib/aarch64-linux-gnu/gstreamer1.0/gstreamer-1.0/gst-ptp-helper /tmp/gst-ptp-helper; \
  mv /tmp/gst-ptp-helper /usr/lib/aarch64-linux-gnu/gstreamer1.0/gstreamer-1.0/gst-ptp-helper; \
  update-ca-certificates; \
  rm -rf /var/lib/apt/lists/*

RUN set -eux; \
  getent group "${USER_GID}" >/dev/null || groupadd --gid "${USER_GID}" "${USERNAME}"; \
  id -u "${USERNAME}" >/dev/null 2>&1 || useradd -m -u "${USER_UID}" -g "${USER_GID}" -s /bin/bash "${USERNAME}"; \
  getent group video >/dev/null 2>&1 || groupadd video; \
  getent group render >/dev/null 2>&1 || groupadd render; \
  getent group audio >/dev/null 2>&1 || groupadd audio; \
  usermod -aG video,audio,render "${USERNAME}"; \
  mkdir -p /work /tmp; \
  test -p /tmp/pekcomm || mkfifo --mode=640 /tmp/pekcomm; \
  chown -R "${USER_UID}:${USER_GID}" /work /tmp/pekcomm

COPY --from=pek-deployment-build /opt/pek-deps/onnxruntime-arm64/lib /opt/pek-deps/onnxruntime/lib
COPY --from=pek-deployment-build /opt/pek-app /work

EXPOSE 8000
EXPOSE 8001
EXPOSE 9999
EXPOSE 8080
EXPOSE 2222

USER ${USERNAME}
WORKDIR /work

ENTRYPOINT ["/work/scripts/private/deployment-runtime.sh"]

# ==============================================================================
# Cairn Integration Runtime
# ==============================================================================

FROM pek-build-base AS pek-cairn-build

ARG TARGETARCH
ARG ONNXRUNTIME_VERSION

WORKDIR /work
COPY development development
COPY config/models/yolov11 config/models/yolov11
COPY data/images/GettyImages-1140581459-thumbnail.jpg data/images/GettyImages-1140581459-thumbnail.jpg
COPY --chmod=0755 scripts/build-elements.sh scripts/build-elements.sh
COPY --chmod=0755 scripts/private/install-onnxruntime.sh scripts/private/install-onnxruntime.sh
COPY scripts/private/shtools.sh scripts/private/shtools.sh

RUN set -eux; \
  mkdir -p tools; \
  scripts/private/install-onnxruntime.sh \
    "${ONNXRUNTIME_VERSION}" "${TARGETARCH}" /opt/pek-deps/onnxruntime; \
  PEK_EXECUTORCH=disabled \
  PEK_HAILORT=disabled \
  PEK_NCNN=disabled \
  PEK_ONNXRUNTIME_ROOT=/opt/pek-deps/onnxruntime \
    scripts/build-elements.sh release false

FROM pek-gstreamer-runtime-base AS pek-cairn-runtime

ARG USERNAME=pek
ARG USER_UID=1000
ARG USER_GID=1000

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8 \
  GST_PLUGIN_PATH=/work/runtime \
  LD_LIBRARY_PATH=/work/runtime

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  gir1.2-glib-2.0 \
  gir1.2-gstreamer-1.0 \
  libgirepository-2.0-0 \
  python3 \
  python3-gi \
  python3-gst-1.0; \
  rm -rf /var/lib/apt/lists/*

RUN set -eux; \
  getent group "${USER_GID}" >/dev/null || groupadd --gid "${USER_GID}" "${USERNAME}"; \
  id -u "${USERNAME}" >/dev/null 2>&1 || useradd -m -u "${USER_UID}" -g "${USER_GID}" -s /bin/bash "${USERNAME}"

WORKDIR /work
COPY --from=pek-cairn-build /opt/pek-deps/onnxruntime/lib/ runtime/
COPY --from=pek-cairn-build \
  /work/development/build/meson-out/libfmt.so \
  /work/development/build/meson-out/libpek-common.so \
  /work/development/build/meson-out/libpekinfer.so \
  /work/development/build/meson-out/pek-onnx-ops.so \
  /work/development/build/meson-out/pek-runtime.so \
  /work/development/build/meson-out/pek-std-ops.so \
  runtime/
COPY --from=pek-cairn-build /work/config/models/yolov11/ config/models/yolov11/
COPY --from=pek-cairn-build \
  /work/data/images/GettyImages-1140581459-thumbnail.jpg \
  data/images/GettyImages-1140581459-thumbnail.jpg

USER ${USERNAME}
