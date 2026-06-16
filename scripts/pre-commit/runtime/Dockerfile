FROM python:3.12-slim-trixie

ENV DEBIAN_FRONTEND=noninteractive \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        git \
        shfmt && \
    rm -rf /var/lib/apt/lists/*

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
