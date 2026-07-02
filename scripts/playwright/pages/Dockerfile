FROM python:3.13-slim-trixie

ARG GH_VERSION=2.82.1

RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends ca-certificates curl git; \
    arch="$(dpkg --print-architecture)"; \
    case "${arch}" in \
        amd64) gh_arch="linux_amd64" ;; \
        arm64) gh_arch="linux_arm64" ;; \
        *) echo "Unsupported GitHub CLI architecture: ${arch}" >&2; exit 1 ;; \
    esac; \
    tmp_dir="$(mktemp -d)"; \
    curl -fsSL "https://github.com/cli/cli/releases/download/v${GH_VERSION}/gh_${GH_VERSION}_${gh_arch}.tar.gz" | tar -xz -C "${tmp_dir}"; \
    mv "${tmp_dir}/gh_${GH_VERSION}_${gh_arch}/bin/gh" /usr/local/bin/gh; \
    rm -rf "${tmp_dir}" /var/lib/apt/lists/*; \
    test "$(gh --version | awk 'NR == 1 {print $3}')" = "${GH_VERSION}"
