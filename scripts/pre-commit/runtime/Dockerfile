FROM python:3.12-slim-trixie

ENV DEBIAN_FRONTEND=noninteractive \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

ARG ACTIONLINT_VERSION=1.7.7

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        git \
        shfmt && \
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

COPY tools/expkits-ci /tmp/pek-tools/expkits-ci

RUN python3 -m pip install --no-cache-dir /tmp/pek-tools/expkits-ci && \
    rm -rf /tmp/pek-tools && \
    groupadd --system repo-checks && \
    useradd \
        --system \
        --gid repo-checks \
        --create-home \
        --home-dir /home/repo-checks \
        --shell /bin/sh \
        repo-checks

ENV HOME=/home/repo-checks

USER repo-checks
