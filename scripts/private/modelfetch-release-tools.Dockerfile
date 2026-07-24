FROM debian:trixie-slim@sha256:020c0d20b9880058cbe785a9db107156c3c75c2ac944a6aa7ab59f2add76a7bd

ARG GH_DEBIAN_VERSION=2.46.0-3

RUN set -eux; \
    export DEBIAN_FRONTEND=noninteractive; \
    apt-get update; \
    apt-get install -y --no-install-recommends ca-certificates gh="${GH_DEBIAN_VERSION}"; \
    rm -rf /var/lib/apt/lists/*; \
    test "$(gh --version | awk 'NR == 1 {print $3}')" = "${GH_DEBIAN_VERSION%%-*}"

USER 65532:65532
