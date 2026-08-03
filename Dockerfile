# syntax=docker/dockerfile:1

ARG BUILDPLATFORM
ARG TARGETARCH
FROM --platform=${BUILDPLATFORM} debian:trixie-slim AS pek-build-base

ARG ONNXRUNTIME_VERSION

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8 \
  PIP_DISABLE_PIP_VERSION_CHECK=1 \
  PYTHONDONTWRITEBYTECODE=1 \
  LD_LIBRARY_PATH=/opt/pek-deps/onnxruntime/lib

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  ca-certificates curl git \
  build-essential meson ninja-build pkg-config cmake unzip \
  python3 \
  libssl-dev libfmt-dev libfftw3-dev libsoup-3.0-dev libjson-glib-dev libcairo2-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev; \
  update-ca-certificates

FROM pek-build-base AS pek-cross-build-base

FROM pek-build-base AS pek-demo-media

ARG NO_EXAMPLE_CONTENT=false

WORKDIR /work
COPY --chmod=0755 scripts/download-data.sh scripts/download-data.sh
RUN if [ "${NO_EXAMPLE_CONTENT}" != "true" ]; then \
      ./scripts/download-data.sh; \
    else \
      mkdir -p data/videos; \
    fi

FROM pek-build-base AS pek-models

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends python3-venv; \
  python3 -m venv /opt/huggingface; \
  /opt/huggingface/bin/pip install --no-cache-dir huggingface_hub==1.18.0

ENV PATH=/opt/huggingface/bin:${PATH}

WORKDIR /work
COPY config config
COPY --chmod=0755 scripts/download-models.py scripts/download-models.py
ARG HF_DOWNLOAD_CACHEBUST
RUN --mount=type=cache,target=/root/.cache/huggingface \
  --mount=type=secret,id=huggingface_token,env=HF_TOKEN \
  HF_DOWNLOAD_CACHEBUST="${HF_DOWNLOAD_CACHEBUST}" \
  ./scripts/download-models.py --models-dir config/models --token "${HF_TOKEN:-}"

FROM pek-cross-build-base AS workspace

ARG TARGETARCH

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
  "${ONNXRUNTIME_VERSION:-}" "${TARGETARCH}" "/opt/pek-deps/onnxruntime-${TARGETARCH}"

WORKDIR /work
COPY development/meson.build development/meson.options development/
COPY development/subprojects/*.wrap development/subprojects/
COPY development/subprojects/packagefiles development/subprojects/packagefiles
RUN meson subprojects download --sourcedir /work/development

COPY scripts/build-elements.sh scripts/build-elements.sh
COPY scripts/private/shtools.sh scripts/private/shtools.sh
COPY scripts/private/deployment-runtime.sh scripts/private/deployment-runtime.sh
COPY development development
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

# Runtime image
FROM debian:trixie-slim AS pek-deployment-base

ARG USERNAME=pek
ARG USER_UID=1000
ARG USER_GID=1000
ARG PEK_PIPELINE=config/pipelines/debug/onnx.json
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
  ca-certificates \
  libssl3t64 libfmt10 libfftw3-single3 libsoup-3.0-0 libjson-glib-1.0-0 libcairo2 \
  libgstreamer1.0-0 gstreamer1.0-tools \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-good \
  gstreamer1.0-nice gstreamer1.0-pipewire; \
  if [ "${PEK_PICAMERA}" = enabled ]; then \
    test "$(dpkg --print-architecture)" = arm64; \
    echo "deb [arch=arm64 trusted=yes] https://archive.raspberrypi.com/debian trixie main" \
    > /etc/apt/sources.list.d/raspberrypi.list; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
    gstreamer1.0-libcamera libcamera-ipa; \
  fi; \
  # Remove the unused PTP helper capability xattr so Docker can import the image on filesystems without capability support. \
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

COPY --from=workspace /opt/pek-deps/onnxruntime-arm64/lib /opt/pek-deps/onnxruntime/lib
COPY --from=workspace /opt/pek-app /work

EXPOSE 8000
EXPOSE 8001
EXPOSE 9999
EXPOSE 8080
EXPOSE 2222

USER ${USERNAME}
WORKDIR /work

ENTRYPOINT ["/work/scripts/private/deployment-runtime.sh"]
