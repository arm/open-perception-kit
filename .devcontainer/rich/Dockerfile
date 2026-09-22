# syntax=docker/dockerfile:1

FROM opk-dev AS opk-dev-rich

ARG PLANTUML_VERSION=1.2026.2
ARG SONAR_SCANNER_VERSION=8.0.1.6346
ARG USERNAME=dev

USER root

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
  --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
  set -eux; \
  apt-get update; \
  apt-get install -y --no-install-recommends \
  openjdk-25-jdk graphviz pandoc doxygen gcovr \
  python3-dev python3-venv python3-gi python3-gst-1.0 \
  libffi-dev zlib1g-dev libbz2-dev liblzma-dev libsqlite3-dev

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
