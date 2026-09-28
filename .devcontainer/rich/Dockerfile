# syntax=docker/dockerfile:1
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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


FROM opk-dev:local AS opk-dev-rich

ARG PLANTUML_VERSION=1.2026.2
ARG SONAR_SCANNER_VERSION=8.0.1.6346
# Container account name, not an authentication credential.
# hadolint ignore=DL3064
ARG USERNAME=dev

USER 0

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  openjdk-25-jdk=25.0.4.1+1-1~deb13u1 graphviz=2.42.4-3 pandoc=3.1.11.1+ds-2 \
  doxygen=1.9.8+ds-2.1 gcovr=7.2+really-1.1 \
  python3-dev=3.13.5-1 python3-venv=3.13.5-1 \
  python3-gi=3.50.0-4+b1 python3-gst-1.0=1.26.2-1 \
  libffi-dev=3.4.8-2 zlib1g-dev=1:1.3.dfsg+really1.3.1-1+b1 \
  libbz2-dev=1.0.8-6 liblzma-dev=5.8.1-1+deb13u1 libsqlite3-dev=3.46.1-7+deb13u2

RUN set -eux; \
  mkdir -p /opt/opk-deps /opt/sonar; \
  plantuml_jar="plantuml-mit-${PLANTUML_VERSION}.jar"; \
  curl --location -fsSLo "/opt/opk-deps/${plantuml_jar}" \
    "https://github.com/plantuml/plantuml/releases/download/v${PLANTUML_VERSION}/${plantuml_jar}"; \
  curl --proto "=https" -fsSLo /tmp/sonar-scanner.zip \
    "https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-${SONAR_SCANNER_VERSION}.zip"; \
  unzip -q /tmp/sonar-scanner.zip -d /opt/sonar; \
  rm -f /tmp/sonar-scanner.zip; \
  touch /container_env

COPY --chmod=0755 .devcontainer/rich/start.sh /usr/local/bin/opk-rich-start

ENV SONAR_HOST_URL=https://sonarqube.mobilestudio.aws.arm.com \
  PATH=/opt/sonar/sonar-scanner-${SONAR_SCANNER_VERSION}/bin:${PATH}

USER ${USERNAME}
WORKDIR /work
