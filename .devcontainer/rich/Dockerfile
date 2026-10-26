# syntax=docker/dockerfile:1
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


# APT revisions follow the selected distribution so security updates stay resolvable.
# hadolint global ignore=DL3008

FROM opk-dev:local AS opk-dev-rich

# Match the docs image and the versioned JAR path used by gen-doc.sh, setup.sh and VS Code.
ARG PLANTUML_VERSION=1.2026.2
# Container account name, not an authentication credential.
# hadolint ignore=DL3064
ARG USERNAME=dev

USER 0

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  default-jdk graphviz pandoc \
  doxygen gcovr \
  python3-dev python3-venv \
  python3-gi python3-gst-1.0 \
  libffi-dev zlib1g-dev \
  libbz2-dev liblzma-dev libsqlite3-dev

RUN set -eux; \
  mkdir -p /opt/opk-deps; \
  plantuml_jar="plantuml-mit-${PLANTUML_VERSION}.jar"; \
  curl --location --retry 3 --retry-all-errors --retry-delay 2 -fsSLo "/opt/opk-deps/${plantuml_jar}" \
    "https://github.com/plantuml/plantuml/releases/download/v${PLANTUML_VERSION}/${plantuml_jar}"; \
  touch /container_env

COPY --chmod=0755 .devcontainer/rich/start.sh /usr/local/bin/opk-rich-start

USER ${USERNAME}
WORKDIR /work
