ARG BUILDPLATFORM
FROM --platform=${BUILDPLATFORM} debian:trixie-slim AS workspace

ARG ONNXRUNTIME_VERSION=1.24.4

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8 \
  PIP_DISABLE_PIP_VERSION_CHECK=1 \
  PYTHONDONTWRITEBYTECODE=1 \
  PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig \
  PKG_CONFIG_SYSROOT_DIR=/ \
  LD_LIBRARY_PATH=/opt/pek-deps/onnxruntime/lib

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  dpkg --add-architecture arm64; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  ca-certificates curl git \
  build-essential meson ninja-build pkg-config cmake \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  libssl-dev:arm64 libfmt-dev:arm64 libfftw3-dev:arm64 libsoup-3.0-dev:arm64 libjson-glib-dev:arm64 libcairo2-dev:arm64 \
  libgstreamer1.0-dev:arm64 libgstreamer-plugins-base1.0-dev:arm64 libgstreamer-plugins-bad1.0-dev:arm64 \
  python3; \
  update-ca-certificates

RUN set -eux; \
  ort_dir="onnxruntime-linux-aarch64-${ONNXRUNTIME_VERSION}"; \
  ort_tgz="${ort_dir}.tgz"; \
  ort_url="https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/${ort_tgz}"; \
  tmp_dir="$(mktemp -d)"; \
  curl -fsSL "$ort_url" | tar -xzf - -C "$tmp_dir"; \
  mkdir -p /opt/pek-deps/onnxruntime; \
  cp -r "$tmp_dir/$ort_dir/include" /opt/pek-deps/onnxruntime/; \
  cp -r "$tmp_dir/$ort_dir/lib" /opt/pek-deps/onnxruntime/; \
  rm -rf "$tmp_dir"

WORKDIR /work
COPY development/meson.build development/meson.options development/
COPY development/subprojects/*.wrap development/subprojects/
COPY development/subprojects/packagefiles development/subprojects/packagefiles
RUN meson subprojects download --sourcedir /work/development

COPY scripts/build-elements.sh scripts/build-elements.sh
COPY scripts/private/shtools.sh scripts/private/shtools.sh
COPY scripts/private/deployment-runtime.sh scripts/private/deployment-runtime.sh
COPY development development
COPY config config
COPY data data

RUN set -eux; \
  mkdir -p /work/tools; \
  NINJAFLAGS=-j2 ./scripts/build-elements.sh release false --extra-setup-args=--cross-file=/work/development/cross/aarch64-linux-gnu.ini; \
  mkdir -p /opt/pek-app/development/build/meson-out /opt/pek-app/tools /opt/pek-app/scripts/private; \
  find /work/development/build/meson-out -maxdepth 1 -type f -name "*.so" -exec cp {} /opt/pek-app/development/build/meson-out/ \; ; \
  cp /work/tools/pek-menu /opt/pek-app/tools/; \
  cp /work/scripts/private/deployment-runtime.sh /opt/pek-app/scripts/private/; \
  chmod +x /opt/pek-app/tools/pek-menu /opt/pek-app/scripts/private/deployment-runtime.sh; \
  cp -r /work/config /opt/pek-app/; \
  cp -r /work/data /opt/pek-app/; \
  cp -r /work/development/web /opt/pek-app/development/

# Runtime image
FROM --platform=linux/arm64 debian:trixie-slim AS pek-deployment-base

ARG USERNAME=pek
ARG USER_UID=1000
ARG USER_GID=1000
ARG PEK_PIPELINE=onnx

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
  ca-certificates \
  libssl3t64 libfmt10 libfftw3-single3 libsoup-3.0-0 libjson-glib-1.0-0 libcairo2 \
  libgstreamer1.0-0 gstreamer1.0-tools \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-good \
  gstreamer1.0-nice gstreamer1.0-pipewire; \
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

COPY --from=workspace /opt/pek-deps/onnxruntime/lib /opt/pek-deps/onnxruntime/lib
COPY --from=workspace --chown=${USER_UID}:${USER_GID} /opt/pek-app /work

EXPOSE 8000
EXPOSE 8001
EXPOSE 9999
EXPOSE 8080
EXPOSE 2222

USER ${USERNAME}
WORKDIR /work

ENTRYPOINT ["/work/scripts/private/deployment-runtime.sh"]
