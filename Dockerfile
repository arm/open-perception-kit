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

FROM --platform=${BUILDPLATFORM} python:3.14-slim-trixie AS opk-build-base

ARG EXECUTORCH_VERSION=1.3.1
ARG EXECUTORCH_DEB_REVISION=2
ARG EXECUTORCH_ARTIFACTORY_SERVER=https://artifactory.arm.com:443
ARG EXECUTORCH_ARTIFACTORY_REPOSITORY=ai-expkits-internal.opk-deb
ARG EXECUTORCH_ARTIFACTORY_DISTRIBUTION=trixie
ARG EXECUTORCH_ARTIFACTORY_COMPONENT=main
ARG CRATES_FALLBACK_REGISTRY=https://crates.io/api/v1/crates
ARG NPM_FALLBACK_REGISTRY=https://artifactory.arm.com:443/artifactory/api/npm/mirrors.npmjs_org
ARG PYPI_FALLBACK_REPOSITORY=https://artifactory.arm.com:443/artifactory/api/pypi/ml-opk.pypi

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8 \
  PIP_DISABLE_PIP_VERSION_CHECK=1 \
  PYTHONDONTWRITEBYTECODE=1 \
  LD_LIBRARY_PATH=/opt/opk-deps/onnxruntime/lib

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  case "$(dpkg --print-architecture)" in \
    amd64) ninja_version=1.12.1-1 ;; \
    arm64) ninja_version=1.12.1-1+b1 ;; \
    *) echo "Unsupported build architecture" >&2; exit 1 ;; \
  esac; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  build-essential=12.12 \
  ca-certificates=20250419 \
  cargo=1.85.1+dfsg1-1+deb13u1 \
  ccache=4.11.2-2 \
  cmake=3.31.6-2 \
  curl=8.14.1-2+deb13u5 \
  git=1:2.47.3-0+deb13u1 \
  libfftw3-dev=3.3.10-2+b1 \
  libfmt-dev=10.1.1+ds1-4 \
  libgstreamer-plugins-bad1.0-dev=1.26.2-3+deb13u3 \
  libgstreamer-plugins-base1.0-dev=1.26.2-1+deb13u2 \
  libgstreamer1.0-dev=1.26.2-2 \
  libjson-glib-dev=1.10.6+ds-2 \
  libsoup-3.0-dev=3.6.5-3 \
  libssl-dev=3.5.7-1~deb13u2 \
  ninja-build="${ninja_version}" \
  pkg-config=1.8.1-4 \
  python3=3.13.5-1 \
  python3-dev=3.13.5-1 \
  python3-venv=3.13.5-1 \
  rustc=1.85.1+dfsg1-1+deb13u1 \
  rustfmt=1.85.1+dfsg1-1+deb13u1 \
  unzip=6.0-29+deb13u1; \
  update-ca-certificates

COPY requirements/build.json requirements/meson.txt /opt/opk-deps/requirements/
RUN python3 -m pip install --no-cache-dir -r /opt/opk-deps/requirements/meson.txt

COPY tools/perception/sdk.json /tmp/perception-sdk.json
COPY development/ops-python/runtime.json /tmp/python-ops-runtime.json
COPY --chmod=0755 scripts/private/install-perception-flatbuffers.sh /usr/local/bin/install-perception-flatbuffers
COPY --chmod=0755 scripts/setup-python-ops-runtime.sh /usr/local/bin/setup-python-ops-runtime

RUN set -eux; \
  install-perception-flatbuffers /tmp/perception-sdk.json; \
  mkdir -p /opt/opk-deps/perception-sdk-artifacts; \
  python3 -c 'import json; d=json.load(open("/tmp/perception-sdk.json")); artifacts=[*d["python_build"]["tools"], d["flatbuffers"]["python_wheel"], *d["flatbuffers"]["rust_crates"], d["typescript_build"]["flatbuffers_runtime"]]; [print(a["filename"], a["url"], a["sha256"], a.get("name", ""), a.get("version", ""), sep="\t") for a in artifacts]' | \
  while IFS=$'\t' read -r filename url sha256 name version; do \
    fallback_user_agent='curl'; \
    case "${url}" in \
      https://files.pythonhosted.org/*) \
        fallback_url="${PYPI_FALLBACK_REPOSITORY}/${url#https://files.pythonhosted.org/}" ;; \
      https://static.crates.io/crates/*) \
        fallback_url="${CRATES_FALLBACK_REGISTRY}/${name}/${version}/download"; \
        fallback_user_agent='cargo' ;; \
      https://registry.npmjs.org/*) \
        fallback_url="${NPM_FALLBACK_REGISTRY}/${url#https://registry.npmjs.org/}" ;; \
      *) echo "Unsupported Perception SDK artifact URL: ${url}" >&2; exit 1 ;; \
    esac; \
    destination="/opt/opk-deps/perception-sdk-artifacts/${filename}"; \
    timeout 30s curl \
      --fail --location --proto '=https' --proto-redir '=https' \
      --retry 1 --output "${destination}" "${url}" || \
      timeout 180s curl \
        --fail --location --proto '=https' --proto-redir '=https' \
        --retry 3 --user-agent "${fallback_user_agent}" \
        --output "${destination}" "${fallback_url}"; \
    echo "${sha256}  ${destination}" | sha256sum --check --strict; \
  done; \
  setup-python-ops-runtime \
    --venv /opt/opk-venvs/python-ops-runtime \
    --runtime-json /tmp/python-ops-runtime.json \
    --sdk-json /tmp/perception-sdk.json; \
  rm -f /tmp/perception-sdk.json /tmp/python-ops-runtime.json


FROM opk-build-base AS opk-cross-build-base

# Downloadable demo media is isolated so development and deployment images can
# reuse it without coupling it to source or tool layers.
FROM --platform=${BUILDPLATFORM} debian:trixie-slim AS opk-demo-media

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
    bash=5.2.37-2+b10 ca-certificates=20250419 curl=8.14.1-2+deb13u5; \
  update-ca-certificates

ARG NO_EXAMPLE_CONTENT=false

WORKDIR /work
COPY --chmod=0444 scripts/private/demo-videos.manifest scripts/private/demo-videos.manifest
COPY --chmod=0755 scripts/private/download-demo-videos.sh scripts/private/download-demo-videos.sh
RUN if [ "${NO_EXAMPLE_CONTENT}" != "true" ]; then \
      ./scripts/private/download-demo-videos.sh; \
    else \
      mkdir -p data/videos; \
    fi

# Model artifacts are resolved in a dedicated stage so Hugging Face tokens stay
# scoped to build-time model download.
FROM --platform=${BUILDPLATFORM} python:3.14-slim-trixie AS opk-models

ENV PIP_DISABLE_PIP_VERSION_CHECK=1 \
  PYTHONDONTWRITEBYTECODE=1

COPY requirements/common.txt requirements/models.txt /opt/opk-deps/requirements/
RUN python3 -m pip install --no-cache-dir -r /opt/opk-deps/requirements/models.txt

WORKDIR /work
COPY config config
COPY --chmod=0755 scripts/download-models.py scripts/download-models.py
COPY tools/config_versions.py tools/config_versions.py
ARG HF_DOWNLOAD_CACHEBUST
# BuildKit excludes secret values from cache keys, so anonymous builds must use
# an explicit key too or an authenticated build could reuse their model layer.
RUN --mount=type=cache,target=/root/.cache/huggingface \
  --mount=type=secret,id=huggingface_token,env=HF_TOKEN \
  if [ -z "${HF_DOWNLOAD_CACHEBUST}" ]; then \
    echo "HF_DOWNLOAD_CACHEBUST is required for model image builds" >&2; \
    exit 1; \
  fi; \
  HF_DOWNLOAD_CACHEBUST="${HF_DOWNLOAD_CACHEBUST}" \
  ./scripts/download-models.py --models-dir config/models

# Development base extends the shared native build tooling. OPK source and build
# outputs come from the mounted checkout, not from this image.
FROM opk-build-base AS opk-dev-base

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

ARG NPM_FALLBACK_REGISTRY=https://artifactory.arm.com:443/artifactory/api/npm/mirrors.npmjs_org
ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=1000

COPY tools/perception/sdk.json /tmp/perception-sdk.json
COPY development/ops-python/runtime.json /tmp/python-ops-runtime.json
COPY development/web/package-lock.json /tmp/opk-web-package-lock.json

RUN set -eux; uname -a; cat /etc/os-release; dpkg --print-architecture

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  file=1:5.46-5 gnupg=2.4.7-21+deb13u1 gosu=1.17-3+b4 \
  gstreamer1.0-gl=1.26.2-1+deb13u2 gstreamer1.0-nice=0.1.22-1 gstreamer1.0-pipewire=1.4.2-1 \
  gstreamer1.0-plugins-bad=1.26.2-3+deb13u3 gstreamer1.0-plugins-base=1.26.2-1+deb13u2 \
  gstreamer1.0-plugins-good=1.26.2-1+deb13u2 gstreamer1.0-plugins-ugly=1.26.3-4+deb13u1 \
  gstreamer1.0-tools=1.26.2-2 gstreamer1.0-x=1.26.2-1+deb13u2 lldb-17=1:17.0.6-22+b2 \
  nodejs=20.19.2+dfsg-1+deb13u2 npm=9.2.0~ds1-3 pre-commit=4.2.0-2 \
  python3-gi=3.50.0-4+b1 python3-pip=25.1.1+dfsg-1 \
  shellcheck=0.10.0-1 shfmt=3.8.0-1+b8 sudo=1.9.16p2-3+deb13u2 \
  valgrind=1:3.24.0-3 wget=1.25.0-2 zip=3.0-15+deb13u1; \
  update-ca-certificates

RUN set -eux; \
  esbuild_url="$(node -e 'const lock=require("/tmp/opk-web-package-lock.json"); console.log(lock.packages["node_modules/esbuild-wasm"].resolved)')"; \
  esbuild_integrity="$(node -e 'const lock=require("/tmp/opk-web-package-lock.json"); console.log(lock.packages["node_modules/esbuild-wasm"].integrity)')"; \
  flatbuffers_url="$(node -e 'const config=require("/tmp/perception-sdk.json"); console.log(config.typescript_build.flatbuffers_runtime.url)')"; \
  flatbuffers_sha256="$(node -e 'const config=require("/tmp/perception-sdk.json"); console.log(config.typescript_build.flatbuffers_runtime.sha256)')"; \
  typescript_url="$(node -e 'const config=require("/tmp/perception-sdk.json"); console.log(config.typescript_build.typescript.url)')"; \
  typescript_sha256="$(node -e 'const config=require("/tmp/perception-sdk.json"); console.log(config.typescript_build.typescript.sha256)')"; \
  download_once() { \
    local max_time="$1"; local retries="$2"; local url="$3"; local destination="$4"; \
    timeout "${max_time}" curl \
      --fail --location --proto '=https' --proto-redir '=https' \
      --retry "${retries}" --output "${destination}" "${url}"; \
  }; \
  download() { \
    local url="$1"; local destination="$2"; \
    download_once 30s 1 "${url}" "${destination}" || \
      download_once 180s 3 \
        "${NPM_FALLBACK_REGISTRY}/${url#https://registry.npmjs.org/}" "${destination}"; \
  }; \
  download "${esbuild_url}" /tmp/esbuild-wasm.tgz; \
  download "${flatbuffers_url}" /tmp/flatbuffers.tgz; \
  download "${typescript_url}" /tmp/typescript.tgz; \
  ESBUILD_INTEGRITY="${esbuild_integrity}" node -e 'const crypto=require("crypto"); const fs=require("fs"); const [algorithm, expected]=process.env.ESBUILD_INTEGRITY.split("-", 2); const actual=crypto.createHash(algorithm).update(fs.readFileSync("/tmp/esbuild-wasm.tgz")).digest("base64"); if (actual !== expected) throw new Error("esbuild-wasm integrity mismatch")'; \
  echo "${flatbuffers_sha256}  /tmp/flatbuffers.tgz" | sha256sum --check --strict; \
  echo "${typescript_sha256}  /tmp/typescript.tgz" | sha256sum --check --strict; \
  npm install --global --ignore-scripts --no-audit --no-fund \
    /tmp/esbuild-wasm.tgz /tmp/flatbuffers.tgz /tmp/typescript.tgz; \
  rm -f /tmp/esbuild-wasm.tgz /tmp/flatbuffers.tgz /tmp/typescript.tgz \
    /tmp/opk-web-package-lock.json

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
  curl --location --retry 3 --retry-all-errors --retry-delay 2 -fsSLo \
    "${tmp_dir}/${actionlint_archive}" "${actionlint_base_url}/${actionlint_archive}"; \
  curl --location --retry 3 --retry-all-errors --retry-delay 2 -fsSLo \
    "${tmp_dir}/checksums.txt" "${actionlint_base_url}/actionlint_${ACTIONLINT_VERSION}_checksums.txt"; \
  checksum="$(grep " ${actionlint_archive}$" "${tmp_dir}/checksums.txt")"; \
  echo "${checksum%% *}  ${tmp_dir}/${actionlint_archive}" | sha256sum --check --strict; \
  tar -xzf "${tmp_dir}/${actionlint_archive}" -C "${tmp_dir}" actionlint; \
  install -m 0755 "${tmp_dir}/actionlint" /usr/local/bin/actionlint; \
  rm -rf "${tmp_dir}"; \
  actionlint -version; \
  shellcheck --version

COPY --chmod=0755 scripts/private/install-onnxruntime.sh /usr/local/bin/install-onnxruntime
RUN install-onnxruntime /opt/opk-deps/requirements/build.json

RUN set -eux; \
  getent group "${USER_GID}" >/dev/null || groupadd --gid "${USER_GID}" "${USERNAME}"; \
  id -u "${USERNAME}" >/dev/null 2>&1 || useradd -l -m -u "${USER_UID}" -g "${USER_GID}" -s /bin/bash "${USERNAME}"; \
  getent group video >/dev/null 2>&1 || groupadd video; \
  getent group render >/dev/null 2>&1 || groupadd render; \
  getent group audio >/dev/null 2>&1 || groupadd audio; \
  usermod -aG video,audio,render "${USERNAME}"; \
  mkdir -p /etc/sudoers.d; \
  echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/90-${USERNAME}"; \
  chmod 0440 "/etc/sudoers.d/90-${USERNAME}"; \
  mkdir -p /work; \
  chown -R "${USER_UID}:${USER_GID}" /work; \
  test -p /tmp/opkcomm || mkfifo --mode=640 /tmp/opkcomm; \
  chown "${USERNAME}" /tmp/opkcomm

RUN set -eux; \
  curl --proto "=https" --retry 3 --retry-all-errors --retry-delay 2 \
    -LsSf https://astral.sh/uv/install.sh | \
  env UV_INSTALL_DIR=/usr/local/bin UV_NO_MODIFY_PATH=1 sh; \
  uv --version

COPY tools/opk-ci /tmp/opk-tools/opk-ci
COPY tools/plumber /tmp/opk-tools/plumber
COPY generated/perception/python /tmp/opk-tools/perception
COPY requirements/common.txt requirements/models.txt requirements/sdk.txt /opt/opk-deps/requirements/
RUN set -eux; \
  uv pip install --system --break-system-packages -r /opt/opk-deps/requirements/common.txt; \
  runtime_arch="$(dpkg --print-architecture)"; \
  case "${runtime_arch}" in amd64) runtime_arch=x86_64 ;; arm64) runtime_arch=aarch64 ;; *) exit 1 ;; esac; \
  numpy_wheel="$(python3 -c 'import json, sys; wheel=json.load(open(sys.argv[1]))["numpy"]["wheels"][sys.argv[2]]; print(wheel["url"] + "#sha256=" + wheel["sha256"])' /tmp/python-ops-runtime.json "${runtime_arch}")"; \
  flatbuffers_wheel="$(python3 -c 'import json; wheel=json.load(open("/tmp/perception-sdk.json"))["flatbuffers"]["python_wheel"]; print(wheel["url"] + "#sha256=" + wheel["sha256"])')"; \
  uv venv --system-site-packages /opt/opk-venvs/devtools; \
  uv pip install --python /opt/opk-venvs/devtools/bin/python \
  -c /opt/opk-deps/requirements/sdk.txt \
  /tmp/opk-tools/opk-ci \
  /tmp/opk-tools/perception \
  /tmp/opk-tools/plumber \
  -r /opt/opk-deps/requirements/models.txt \
  "${numpy_wheel}" \
  "${flatbuffers_wheel}"; \
  /opt/opk-venvs/devtools/bin/python -I -c 'import perception, plumber'; \
  chown -R "${USER_UID}:${USER_GID}" /opt/opk-venvs/devtools; \
  rm -rf /tmp/opk-tools /tmp/perception-sdk.json /tmp/python-ops-runtime.json

EXPOSE 8000 8001 9999 8080 2222

ENV GST_DEBUG=2 \
  GST_PLUGIN_PATH=/work/development/build/meson-out \
  LD_LIBRARY_PATH=/opt/opk-deps/onnxruntime/lib:/work/development/build/meson-out \
  OPK_PYTHON_RUNTIME_VENV=/opt/opk-venvs/python-ops-runtime \
  OPK_DEVTOOLS_VENV=/opt/opk-venvs/devtools \
  PATH=/opt/opk-venvs/devtools/bin:${PATH}

USER ${USERNAME}
WORKDIR /work

COPY --chmod=0755 scripts/private/development-entrypoint.sh /usr/local/bin/development-entrypoint
ENTRYPOINT ["/usr/local/bin/development-entrypoint"]

# Developer shell, editor, debugger, and network tooling.
FROM opk-dev-base AS opk-dev-tools

ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=1000
ARG NVIM_VERSION=v0.12.1
ARG CPP_TOOLS_VERSION=v1.29.3
ARG TARGETARCH
ARG EXECUTORCH_ARTIFACTORY_USERNAME=""
ARG EXECUTORCH_ARTIFACTORY_PASSWORD=""

USER root

RUN --mount=type=bind,source=var,target=/tmp/opk-executorch-packages,ro \
    --mount=type=bind,source=scripts/private/executorch/install-executorch-deb.sh,target=/tmp/install-executorch-deb.sh,ro \
  bash /tmp/install-executorch-deb.sh

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  bash-completion=1:2.16.0-7 bat=0.25.0-2+b2 clangd=1:19.0-63 \
  bind9-dnsutils=1:9.20.29-1~deb13u1 eza=0.21.0-1+b1 fd-find=10.2.0-1+b5 \
  ffmpeg=7:7.1.5-0+deb13u1 firefox-esr=140.16.0esr-1~deb13u1 fonts-powerline=2.8.4-1 \
  gdb=16.3-1 gcovr=7.2+really-1.1 iproute2=6.15.0-1 \
  iputils-arping=3:20240905-3 iputils-ping=3:20240905-3 less=668-1 \
  locales=2.41-12+deb13u4 lua5.1=5.1.5-11 luarocks=3.8.0+dfsg1-1 \
  mc=3:4.8.33-1+deb13u1 nano=8.4-1+deb13u1 neovim=0.10.4-8 net-tools=2.10-1.3 \
  nmap=7.95+dfsg-3 openssh-client=1:10.0p1-7+deb13u4 powerline=2.8.4-1 ripgrep=14.1.1-1+b4 \
  tcpdump=4.99.5-2 tmux=3.5a-3 traceroute=1:2.1.6-1 tree-sitter-cli=0.22.6-6+b1 \
  v4l-utils=1.30.1-1 vim=2:9.1.1230-2 wl-clipboard=2.2.1-2 \
  xz-utils=5.8.1-1+deb13u1 zsh=5.9-8+b24; \
  if apt-get install -y --no-install-recommends --dry-run gstreamer1.0-libav=1.26.2-1+deb13u1; then \
    apt-get install -y --no-install-recommends gstreamer1.0-libav=1.26.2-1+deb13u1; \
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
  runuser -u "${USERNAME}" -- \
    git clone --quiet --depth 1 https://github.com/ohmyzsh/ohmyzsh.git "/home/${USERNAME}/.oh-my-zsh"; \
  mkdir -p "/home/${USERNAME}/.config" "/home/${USERNAME}/configs"; \
  ln -sfn "/home/${USERNAME}/configs/zshrc" "/home/${USERNAME}/.zshrc"; \
  ln -sfn "/home/${USERNAME}/configs/nvchad_2026_04" "/home/${USERNAME}/.config/nvim"; \
  chown -R "${USER_UID}:${USER_GID}" "/home/${USERNAME}/.config" "/home/${USERNAME}/configs" "/home/${USERNAME}/.zshrc"

COPY --chmod=0444 .devcontainer/configs/zshrc /home/${USERNAME}/configs/zshrc

ENV LANG=en_US.UTF-8 \
  LC_ALL=en_US.UTF-8 \
  SHELL=/bin/zsh \
  SSH_AUTH_SOCK=/ssh-agent

USER ${USERNAME}

# Final devcontainer image. Contract: tools and dependency libraries only. The
# repository is mounted at /work; OPK binaries are built from that checkout.
FROM opk-dev-tools AS opk-dev

ARG USERNAME=dev
ARG OPK_PICAMERA=disabled

USER root

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  if [ "${OPK_PICAMERA}" = enabled ]; then \
    test "$(dpkg --print-architecture)" = arm64; \
    echo "deb [arch=arm64 trusted=yes] https://archive.raspberrypi.com/debian trixie main" \
    > /etc/apt/sources.list.d/raspberrypi.list; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
    gstreamer1.0-libcamera=0.7.2+rpt20260817-1 libcamera-ipa=0.7.2+rpt20260817-1; \
  fi

RUN set -eux; \
  rm -rf /opt/opk-deps/onnxruntime-arm64; \
  if [ "$(dpkg --print-architecture)" = arm64 ]; then \
    ln -s onnxruntime /opt/opk-deps/onnxruntime-arm64; \
  else \
    install-onnxruntime /opt/opk-deps/requirements/build.json arm64 /opt/opk-deps/onnxruntime-arm64; \
  fi

USER ${USERNAME}
WORKDIR /work
COPY --from=opk-demo-media \
  /work/data/videos /opt/opk-app/data/videos
COPY --from=opk-models \
  /work/config/models /opt/opk-app/config/models

# Prewarm the macOS CI compiler cache on the native Arm64 image publisher.
FROM opk-dev AS opk-dev-macos-cache-build

USER root
RUN install -d /opt/opk-ccache

ENV CCACHE_DIR=/opt/opk-ccache \
  CCACHE_MAXSIZE=2G

RUN --mount=type=bind,source=.,target=/work,rw \
  ./scripts/build.sh && ccache --show-stats
USER dev

FROM opk-dev AS opk-dev-macos-ci

USER root
COPY --from=opk-dev-macos-cache-build /opt/opk-ccache /opt/opk-ccache
USER dev

# ==============================================================================
# Documentation Image Lane
# ==============================================================================

FROM opk-dev-base AS opk-docs

ARG USERNAME=dev
ARG PLANTUML_VERSION=1.2026.2

USER root

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  doxygen=1.9.8+ds-2.1 graphviz=2.42.4-3 \
  openjdk-25-jdk=25.0.4.1+1-1~deb13u1 pandoc=3.1.11.1+ds-2

RUN set -eux; \
  mkdir -p /opt/opk-deps; \
  plantuml_jar="plantuml-mit-${PLANTUML_VERSION}.jar"; \
  plantuml_base_url="https://github.com/plantuml/plantuml/releases/download/v${PLANTUML_VERSION}"; \
  curl --location -fsSLo "/opt/opk-deps/${plantuml_jar}" "${plantuml_base_url}/${plantuml_jar}"

USER ${USERNAME}
WORKDIR /work

# ==============================================================================
# Deployment Build and Runtime Images
# ==============================================================================

FROM python:3.14-slim-trixie AS opk-gstreamer-runtime-base

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  ca-certificates=20250419 \
  gstreamer1.0-plugins-base=1.26.2-1+deb13u2 \
  gstreamer1.0-tools=1.26.2-2 \
  libgstreamer1.0-0=1.26.2-2; \
  update-ca-certificates; \
  rm -rf /var/lib/apt/lists/*

FROM opk-gstreamer-runtime-base AS opk-python-ops-runtime

ENV PIP_DISABLE_PIP_VERSION_CHECK=1 \
  PYTHONDONTWRITEBYTECODE=1

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
    python3=3.13.5-1 \
    python3-venv=3.13.5-1; \
  update-ca-certificates; \
  rm -rf /var/lib/apt/lists/*

COPY tools/perception/sdk.json /tmp/perception-sdk.json
COPY development/ops-python/runtime.json /tmp/python-ops-runtime.json
COPY --chmod=0755 scripts/setup-python-ops-runtime.sh /usr/local/bin/setup-python-ops-runtime

RUN set -eux; \
  setup-python-ops-runtime \
    --venv /opt/opk-venvs/python-ops-runtime \
    --runtime-json /tmp/python-ops-runtime.json \
    --sdk-json /tmp/perception-sdk.json; \
  rm -rf \
    /tmp/perception-sdk.json \
    /tmp/python-ops-runtime.json

COPY generated/perception/python /tmp/perception-python
RUN set -eux; \
  /opt/opk-venvs/python-ops-runtime/bin/pip install --no-cache-dir --no-deps \
    /tmp/perception-python; \
  /opt/opk-venvs/python-ops-runtime/bin/python -c 'import perception'; \
  rm -rf /tmp/perception-python

FROM opk-cross-build-base AS opk-deployment-build

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

ARG TARGETARCH
ARG NO_EXAMPLE_CONTENT=false
ARG OPK_RELEASE_BUILD=false
ARG OPK_RELEASE_SOURCE_COMMIT=""

COPY --chmod=0755 scripts/private/install-target-sysroot.sh /usr/local/bin/install-target-sysroot
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  case "${TARGETARCH}" in amd64 | arm64) ;; *) exit 1 ;; esac; \
  if [ "${TARGETARCH}" != "$(dpkg --print-architecture)" ]; then \
    test "${TARGETARCH}" = arm64; \
    install-target-sysroot "${TARGETARCH}"; \
  elif [ "${OPK_RELEASE_BUILD}" = true ]; then \
    apt-get update; \
    apt-get install -y --no-install-recommends \
      binutils=2.44-3 libusb-1.0-0-dev=2:1.0.28-1 zlib1g-dev=1:1.3.dfsg+really1.3.1-1+b1; \
  fi

COPY --chmod=0755 scripts/private/install-onnxruntime.sh /usr/local/bin/install-onnxruntime
RUN install-onnxruntime \
  /opt/opk-deps/requirements/build.json "${TARGETARCH}" /opt/opk-deps/onnxruntime

RUN --mount=type=bind,source=var,target=/tmp/opk-executorch-packages,ro \
    --mount=type=bind,source=scripts/private/executorch/install-executorch-deb.sh,target=/tmp/install-executorch-deb.sh,ro \
    --mount=type=secret,id=executorch_artifactory_username \
    --mount=type=secret,id=executorch_artifactory_password \
  set -eu; \
  if [ "${OPK_RELEASE_BUILD}" = true ]; then \
    test "${TARGETARCH}" = "$(dpkg --print-architecture)"; \
    EXECUTORCH_ARTIFACTORY_USERNAME="$(cat /run/secrets/executorch_artifactory_username)"; \
    EXECUTORCH_ARTIFACTORY_PASSWORD="$(cat /run/secrets/executorch_artifactory_password)"; \
    export EXECUTORCH_ARTIFACTORY_USERNAME EXECUTORCH_ARTIFACTORY_PASSWORD; \
    bash /tmp/install-executorch-deb.sh; \
  fi

WORKDIR /work
COPY development/meson.build development/meson.options development/
COPY development/subprojects/*.wrap development/subprojects/
COPY development/subprojects/packagefiles development/subprojects/packagefiles
RUN meson subprojects download --sourcedir /work/development

COPY scripts/build.sh scripts/build.sh
COPY scripts/private/shtools.sh scripts/private/shtools.sh
COPY scripts/private/deployment-runtime.sh scripts/private/deployment-runtime.sh
COPY --chmod=0755 scripts/perception-sdk.sh scripts/perception-sdk.sh
COPY --chmod=0755 scripts/private/run-perception-sdk.sh scripts/private/run-perception-sdk.sh
COPY scripts/release/ReleaseTool.py scripts/release/ReleaseTool.py
COPY .clang-format .cmake-format.yaml ./
COPY tools/config_versions.py tools/config_versions.py
COPY tools/perception tools/perception
COPY tools/flowdata-sdk tools/flowdata-sdk
COPY schemas/perception/metadata schemas/perception/metadata
COPY development development
COPY generated generated
COPY --from=opk-models /work/config config

RUN --mount=type=cache,id=opk-deployment-ccache,target=/work/.cache/ccache,sharing=locked \
  set -eux; \
  export CCACHE_DIR=/work/.cache/ccache; \
  export CCACHE_MAXSIZE=2G; \
  export CCACHE_UMASK=000; \
  ccache --zero-stats; \
  /opt/opk-venvs/python-ops-runtime/bin/pip install --no-cache-dir --no-deps \
    /work/generated/perception/python; \
  /opt/opk-venvs/python-ops-runtime/bin/python -c \
    'import flatbuffers, numpy, perception'; \
  native_arch="$(dpkg --print-architecture)"; \
  extra_setup_args=(); \
  if [ "${OPK_RELEASE_BUILD}" = true ]; then \
    extra_setup_args=("--extra-setup-args=-Drelease_package=true,-Dprefix=/,-Dlibdir=lib"); \
  elif [ "${TARGETARCH}" != "${native_arch}" ]; then \
    extra_setup_args=("--extra-setup-args=--cross-file=/work/development/cross/aarch64-linux-gnu.ini"); \
  fi; \
  mkdir -p /work/tools; \
  executorch=auto; \
  python_ops=auto; \
  if [ "${OPK_RELEASE_BUILD}" = true ]; then \
    executorch=enabled; \
    python_ops=enabled; \
  elif [ "${TARGETARCH}" != "${native_arch}" ]; then \
    python_ops=disabled; \
  fi; \
  OPK_EXECUTORCH="${executorch}" \
  OPK_PYTHON_OPS="${python_ops}" \
  OPK_PYTHON_RUNTIME_VENV=/opt/opk-venvs/python-ops-runtime \
  OPK_ONNXRUNTIME_ROOT=/opt/opk-deps/onnxruntime \
  NINJAFLAGS=-j2 \
  ./scripts/build.sh release false "${extra_setup_args[@]}"; \
  ccache --show-stats; \
  mkdir -p /opt/opk-app/development/build/meson-out /opt/opk-app/tools /opt/opk-app/scripts/private; \
  find /work/development/build \
    -path /work/development/build/subprojects -prune -o \
    -type f -name "*.so" -exec cp {} /opt/opk-app/development/build/meson-out/ \; ; \
  cp /work/tools/opk-menu /opt/opk-app/tools/; \
  cp /work/scripts/private/deployment-runtime.sh /opt/opk-app/scripts/private/; \
  chmod +x /opt/opk-app/tools/opk-menu /opt/opk-app/scripts/private/deployment-runtime.sh; \
  mkdir -p /opt/opk-release-artifacts; \
  if [ "${OPK_RELEASE_BUILD}" = true ]; then \
    case "${TARGETARCH}" in \
      amd64) architecture=x86_64 ;; \
      arm64) architecture=aarch64 ;; \
    esac; \
    package_root=/opt/opk-release-root; \
    test -n "${OPK_RELEASE_SOURCE_COMMIT}"; \
    /work/scripts/perception-sdk.sh package \
      --output-dir /tmp/perception-sdk-input \
      --artifact-dir /opt/opk-deps/perception-sdk-artifacts \
      --repository-commit "${OPK_RELEASE_SOURCE_COMMIT}"; \
    mkdir -p \
      "${package_root}/lib/opk" \
      "${package_root}/share/opk/licenses/libexecutorch-dev" \
      "${package_root}/share/opk/perception-sdk"; \
    /work/tools/opk-config-check --root /work; \
    DESTDIR="${package_root}" meson install \
      -C /work/development/build --skip-subprojects; \
    /opt/opk-venvs/python-ops-runtime/bin/python \
      /work/scripts/release/ReleaseTool.py stage-python-runtime \
      --stage-root "${package_root}"; \
    onnxruntime_version="$(python3 -c 'import json; print(json.load(open("/opt/opk-deps/requirements/build.json"))["onnxruntime"])')"; \
    cp "/opt/opk-deps/onnxruntime/lib/libonnxruntime.so.${onnxruntime_version}" \
      "${package_root}/lib/opk/"; \
    ln -s "libonnxruntime.so.${onnxruntime_version}" \
      "${package_root}/lib/opk/libonnxruntime.so.1"; \
    cp -a /opt/opk-deps/onnxruntime/share/doc/onnxruntime/. \
      "${package_root}/share/opk/licenses/"; \
    cp -a /opt/opk-deps/executorch-legal-documentation/. \
      "${package_root}/share/opk/licenses/libexecutorch-dev/"; \
    cp -a /tmp/perception-sdk-input/. \
      "${package_root}/share/opk/perception-sdk/"; \
    python3 /work/scripts/release/ReleaseTool.py stage-models \
      --repo-root /work --stage-root "${package_root}"; \
    python3 /work/scripts/release/ReleaseTool.py validate-package \
      --architecture "${architecture}" \
      --expected-commit "${OPK_RELEASE_SOURCE_COMMIT}" \
      --repo-root /work --package-root "${package_root}"; \
    rm -rf /tmp/perception-sdk-input; \
  fi; \
  rm -rf /work/development/build

ARG OPK_RELEASE_BUILD_ID=""
RUN set -eux; \
  if [ -n "${OPK_RELEASE_BUILD_ID}" ]; then \
    test "${OPK_RELEASE_BUILD}" = true; \
    case "${TARGETARCH}" in \
      amd64) architecture=x86_64 ;; \
      arm64) architecture=aarch64 ;; \
    esac; \
    package_name="opk-${OPK_RELEASE_BUILD_ID}-linux-${architecture}"; \
    package_root="/tmp/opk-release/${package_name}"; \
    mkdir -p /tmp/opk-release; \
    cp -a /opt/opk-release-root "${package_root}"; \
    archive="/opt/opk-release-artifacts/${package_name}.tar.gz"; \
    tar -C /tmp/opk-release -czf "${archive}" "${package_name}"; \
    sha256sum "${archive}"; \
    rm -rf /tmp/opk-release; \
  fi

FROM opk-python-ops-runtime AS opk-deployment-base

ARG USERNAME=opk
ARG USER_UID=1000
ARG USER_GID=1000
ARG OPK_PIPELINE=yolo26-onnx
ARG OPK_PICAMERA=disabled
ARG BUILDARCH
ARG TARGETARCH

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8 \
  GST_DEBUG=2 \
  GST_PLUGIN_PATH=/work/development/build/meson-out \
  LD_LIBRARY_PATH=/opt/opk-deps/onnxruntime/lib:/work/development/build/meson-out \
  OPK_PYTHON_RUNTIME_VENV=/opt/opk-venvs/python-ops-runtime \
  OPK_PIPELINE=${OPK_PIPELINE}

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  gstreamer1.0-nice=0.1.22-1 \
  gstreamer1.0-pipewire=1.4.2-1 \
  gstreamer1.0-plugins-bad=1.26.2-3+deb13u3 \
  gstreamer1.0-plugins-good=1.26.2-1+deb13u2 \
  libfftw3-single3=3.3.10-2+b1 \
  libfmt10=10.1.1+ds1-4 \
  libjson-glib-1.0-0=1.10.6+ds-2 \
  libsoup-3.0-0=3.6.5-3 \
  libssl3t64=3.5.7-1~deb13u2 \
  libusb-1.0-0=2:1.0.28-1 \
  python3=3.13.5-1 \
  zlib1g=1:1.3.dfsg+really1.3.1-1+b1; \
  if [ "${OPK_PICAMERA}" = enabled ]; then \
    test "$(dpkg --print-architecture)" = arm64; \
    echo "deb [arch=arm64 trusted=yes] https://archive.raspberrypi.com/debian trixie main" \
    > /etc/apt/sources.list.d/raspberrypi.list; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
    gstreamer1.0-libcamera=0.7.2+rpt20260817-1 libcamera-ipa=0.7.2+rpt20260817-1; \
  fi; \
  ptp_helpers=(/usr/lib/*-linux-gnu/gstreamer1.0/gstreamer-1.0/gst-ptp-helper); \
  test "${#ptp_helpers[@]}" -eq 1; \
  install -m 0755 "${ptp_helpers[0]}" /tmp/gst-ptp-helper; \
  mv /tmp/gst-ptp-helper "${ptp_helpers[0]}"; \
  update-ca-certificates; \
  rm -rf /var/lib/apt/lists/*

RUN set -eux; \
  getent group "${USER_GID}" >/dev/null || groupadd --gid "${USER_GID}" "${USERNAME}"; \
  id -u "${USERNAME}" >/dev/null 2>&1 || useradd -l -m -u "${USER_UID}" -g "${USER_GID}" -s /bin/bash "${USERNAME}"; \
  getent group video >/dev/null 2>&1 || groupadd video; \
  getent group render >/dev/null 2>&1 || groupadd render; \
  getent group audio >/dev/null 2>&1 || groupadd audio; \
  usermod -aG video,audio,render "${USERNAME}"; \
  mkdir -p /work /tmp; \
  test -p /tmp/opkcomm || mkfifo --mode=640 /tmp/opkcomm; \
  chown -R "${USER_UID}:${USER_GID}" /work /tmp/opkcomm

COPY --from=opk-deployment-build /opt/opk-deps/onnxruntime/lib /opt/opk-deps/onnxruntime/lib
COPY --from=opk-deployment-build /work/config /work/config
COPY data /work/data
COPY --from=opk-demo-media /work/data/videos /work/data/videos
COPY development/web /work/development/web
COPY --from=opk-deployment-build /opt/opk-app/development/build /work/development/build
COPY --from=opk-deployment-build /opt/opk-app/tools /work/tools
COPY --from=opk-deployment-build /opt/opk-app/scripts /work/scripts
COPY --chmod=0755 scripts/release/smoke-opk-package.sh /work/scripts/release/smoke-opk-package.sh
COPY --from=opk-deployment-build /opt/opk-release-artifacts /opt/opk-release-artifacts

RUN set -eux; \
  /opt/opk-venvs/python-ops-runtime/bin/python -c \
    'import flatbuffers, numpy, perception'; \
  python_ops=/work/development/build/meson-out/opk-python-ops.so; \
  if [ "${BUILDARCH}" = "${TARGETARCH}" ]; then \
    test -f "${python_ops}"; \
    if ldd "${python_ops}" | grep -q 'not found'; then \
      exit 1; \
    fi; \
    gst-launch-1.0 -q \
      videotestsrc num-buffers=1 pattern=ball ! \
      videoconvert ! videoscale ! \
      video/x-raw,format=BGRA,width=320,height=240,framerate=5/1 ! \
      opkinfer \
        opchain-path=/work/config/models/mobilenetv2/opchain-python-classification.json \
        active=true ! \
      fakesink; \
  else \
    test ! -e "${python_ops}"; \
  fi

EXPOSE 8000
EXPOSE 8001
EXPOSE 9999
EXPOSE 8080
EXPOSE 2222

USER ${USERNAME}
WORKDIR /work

ARG TARGETARCH
ARG OPK_RELEASE_BUILD_ID=""
RUN --network=none \
  --mount=type=bind,source=development/tests/python_script_op/runtime_environment.py,target=/tmp/runtime_environment.py,readonly \
  set -eux; \
  if [ -z "${OPK_RELEASE_BUILD_ID}" ]; then \
    exit 0; \
  fi; \
  case "${TARGETARCH}" in \
    amd64) architecture=x86_64 ;; \
    arm64) architecture=aarch64 ;; \
    *) exit 1 ;; \
  esac; \
  package_name="opk-${OPK_RELEASE_BUILD_ID}-linux-${architecture}"; \
  /work/scripts/release/smoke-opk-package.sh \
    "/opt/opk-release-artifacts/${package_name}.tar.gz" \
    /tmp/runtime_environment.py

ENTRYPOINT ["/work/scripts/private/deployment-runtime.sh"]

# ==============================================================================
# Cairn Integration Runtime
# ==============================================================================

FROM opk-build-base AS opk-cairn-build

ARG TARGETARCH

WORKDIR /work
COPY development development
COPY config/models/yolov11 config/models/yolov11
COPY data/images/GettyImages-1140581459-thumbnail.jpg data/images/GettyImages-1140581459-thumbnail.jpg
COPY --chmod=0755 scripts/build.sh scripts/build.sh
COPY --chmod=0755 scripts/private/install-onnxruntime.sh scripts/private/install-onnxruntime.sh
COPY scripts/private/shtools.sh scripts/private/shtools.sh

RUN set -eux; \
  mkdir -p tools; \
  scripts/private/install-onnxruntime.sh \
    /opt/opk-deps/requirements/build.json "${TARGETARCH}" /opt/opk-deps/onnxruntime; \
  OPK_EXECUTORCH=disabled \
  OPK_ONNXRUNTIME_ROOT=/opt/opk-deps/onnxruntime \
    scripts/build.sh release false

FROM opk-gstreamer-runtime-base AS opk-cairn-runtime

ARG USERNAME=opk
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
  gir1.2-glib-2.0=2.84.4-3~deb13u5 \
  gir1.2-gstreamer-1.0=1.26.2-2 \
  libgirepository-2.0-0=2.84.4-3~deb13u5 \
  python3=3.13.5-1 \
  python3-gi=3.50.0-4+b1 \
  python3-gst-1.0=1.26.2-1; \
  rm -rf /var/lib/apt/lists/*

RUN set -eux; \
  getent group "${USER_GID}" >/dev/null || groupadd --gid "${USER_GID}" "${USERNAME}"; \
  id -u "${USERNAME}" >/dev/null 2>&1 || useradd -l -m -u "${USER_UID}" -g "${USER_GID}" -s /bin/bash "${USERNAME}"

WORKDIR /work
COPY --from=opk-cairn-build /opt/opk-deps/onnxruntime/lib/ runtime/
COPY --from=opk-cairn-build \
  /work/development/build/meson-out/libfmt.so \
  /work/development/build/meson-out/libopk-common.so \
  /work/development/build/meson-out/libopkinfer.so \
  /work/development/build/meson-out/opk-onnx-ops.so \
  /work/development/build/meson-out/opk-runtime.so \
  /work/development/build/meson-out/opk-std-ops.so \
  runtime/
COPY --from=opk-cairn-build /work/config/models/yolov11/ config/models/yolov11/
COPY --from=opk-cairn-build \
  /work/data/images/GettyImages-1140581459-thumbnail.jpg \
  data/images/GettyImages-1140581459-thumbnail.jpg

USER ${USERNAME}
