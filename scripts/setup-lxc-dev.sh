#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Installs the OPK Docker development-image contract into an existing LXC.
################################################################

set -euo pipefail

hf_token="${HF_TOKEN-}"
export -n hf_token
unset HF_TOKEN

usage() {
    cat << 'EOF'
Usage:
  sudo --preserve-env=OPK_PROJECT_ROOT,HF_TOKEN,NPM_FALLBACK_REGISTRY \
    ./scripts/setup-lxc-dev.sh [options]

Installs the dependencies and tooling from the Dockerfile's opk-dev target
directly into a Debian 13 (trixie) LXC container. OPK_PROJECT_ROOT must be an
absolute path when set; otherwise the checkout containing this script is used.

Options:
  --user NAME       Configure NAME as the development user (default: SUDO_USER)
  --skip-assets     Do not download model files and demo videos
  --skip-shell      Do not install Neovim, cpptools
  -h, --help        Show this help

The LXC host remains responsible for networking and bind-mounting the checkout,
/dev/dri, camera/audio devices, the user's configs, and SSH credentials.
HF_TOKEN is exposed only to the model downloader.
EOF
}

log() {
    printf '[setup-lxc-dev] %s\n' "$*"
}

die() {
    printf '[setup-lxc-dev] ERROR: %s\n' "$*" >&2
    exit 1
}

DEV_USER="${SUDO_USER:-ibori}"
SKIP_ASSETS=false
SKIP_SHELL=false
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --user)
            [[ $# -ge 2 ]] || die "--user requires a value"
            DEV_USER="$2"
            shift
            ;;
        --skip-assets)
            SKIP_ASSETS=true
            ;;
        --skip-shell)
            SKIP_SHELL=true
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
    shift
done

[[ "$(id -u)" -eq 0 ]] || die "run this script as root (normally with sudo)"
[[ -f /etc/os-release ]] || die "/etc/os-release is missing"
# shellcheck disable=SC1091
. /etc/os-release
[[ "${ID:-}" == debian && "${VERSION_CODENAME:-}" == trixie ]] ||
    die "this script supports Debian 13 (trixie), found ${PRETTY_NAME:-unknown}"
[[ "$(dpkg --print-architecture)" == amd64 ]] ||
    die "this LXC setup currently supports amd64 only"

requested_project_root="${OPK_PROJECT_ROOT:-$SCRIPT_DIR/..}"
[[ "$requested_project_root" == /* ]] ||
    die "OPK_PROJECT_ROOT must be an absolute path: $requested_project_root"
[[ -d "$requested_project_root" ]] ||
    die "OPK project root does not exist: $requested_project_root"
OPK_PROJECT_ROOT="$(cd -- "$requested_project_root" && pwd -P)"
export OPK_PROJECT_ROOT
[[ -e "$OPK_PROJECT_ROOT/.git" && -f "$OPK_PROJECT_ROOT/Dockerfile" ]] ||
    die "OPK_PROJECT_ROOT is not an OPK checkout: $OPK_PROJECT_ROOT"

id "$DEV_USER" > /dev/null 2>&1 || die "development user does not exist: $DEV_USER"

DEV_HOME="$(getent passwd "$DEV_USER" | cut -d: -f6)"
DEV_GROUP="$(id -gn "$DEV_USER")"
ACTIONLINT_VERSION=1.7.12
UV_VERSION=0.12.3
NVIM_VERSION=v0.12.1
CPP_TOOLS_VERSION=v1.29.3
NPM_FALLBACK_REGISTRY="${NPM_FALLBACK_REGISTRY:-https://artifactory.arm.com:443/artifactory/api/npm/mirrors.npmjs_org}"

export DEBIAN_FRONTEND=noninteractive

log "OPK project root: $OPK_PROJECT_ROOT"
log "Installing Debian packages"
apt-get update
apt-get install -y --no-install-recommends \
    bash-completion bat build-essential ca-certificates clangd cmake curl \
    dnsutils eza fd-find file firefox-esr fonts-powerline gdb git gnupg gosu \
    gstreamer1.0-gl gstreamer1.0-nice gstreamer1.0-pipewire \
    gstreamer1.0-plugins-bad gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly gstreamer1.0-tools \
    gstreamer1.0-x iproute2 iputils-arping iputils-ping less libfftw3-dev \
    libfmt-dev libgstreamer-plugins-bad1.0-dev \
    libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev libjson-glib-dev \
    libsoup-3.0-dev libssl-dev lldb-17 locales lua5.1 luarocks mc meson nano \
    neovim net-tools ninja-build nmap nodejs openssh-client pkg-config \
    powerline pre-commit python3 python3-dev python3-gi python3-pip \
    python3-venv ripgrep shellcheck shfmt sudo tcpdump tmux traceroute \
    tree-sitter-cli unzip v4l-utils valgrind vim wget wl-clipboard xz-utils \
    zip zsh

# Debian packages npm separately, while packages from repositories such as
# NodeSource bundle npm into nodejs and declare a conflict with Debian's npm.
# Reused LXCs may already have the latter, so only install npm when nodejs did
# not provide it.
if ! command -v npm > /dev/null 2>&1; then
    apt-get install -y --no-install-recommends npm
fi
npm --version
# The single-quoted program contains a JavaScript template literal, not a shell
# parameter expansion.
# shellcheck disable=SC2016
node -e '
const path = require("path");
const projectRoot = process.env.OPK_PROJECT_ROOT;
const minimum = require(path.join(projectRoot, "tools/perception/sdk.json"))
    .typescript_build.node_minimum_major;
const actual = Number(process.versions.node.split(".")[0]);
if (actual < minimum) {
    throw new Error(`Node.js ${minimum} or newer is required; found ${process.version}`);
}
'

if apt-get install -y --no-install-recommends --dry-run gstreamer1.0-libav > /dev/null 2>&1; then
    apt-get install -y --no-install-recommends gstreamer1.0-libav
else
    log "NOTE: gstreamer1.0-libav is unavailable from the configured mirror"
fi
update-ca-certificates
sed -i 's/^# *\(en_US.UTF-8 UTF-8\)/\1/' /etc/locale.gen
locale-gen en_US.UTF-8
update-locale LANG=en_US.UTF-8

ln -sfn /usr/bin/lldb-17 /usr/local/bin/lldb
ln -sfn /usr/bin/lldb-server-17 /usr/local/bin/lldb-server
for group in video render audio; do
    getent group "$group" > /dev/null || groupadd "$group"
done
usermod -aG video,render,audio "$DEV_USER"
chsh -s /usr/bin/zsh "$DEV_USER"

log "Installing the pinned Perception FlatBuffers toolchain"
bash "$OPK_PROJECT_ROOT/scripts/private/install-perception-flatbuffers.sh" \
    "$OPK_PROJECT_ROOT/tools/perception/sdk.json"

log "Installing the pinned ONNX Runtime"
bash "$OPK_PROJECT_ROOT/scripts/private/install-onnxruntime.sh" \
    "$OPK_PROJECT_ROOT/requirements/build.json"
bash "$OPK_PROJECT_ROOT/scripts/private/install-onnxruntime.sh" \
    "$OPK_PROJECT_ROOT/requirements/build.json" arm64 /opt/opk-deps/onnxruntime-arm64

log "Installing ExecuTorch when a local package or Artifactory credentials are available"
EXECUTORCH_DEB_PACKAGE_DIR="$OPK_PROJECT_ROOT/var" \
    EXECUTORCH_ARTIFACTORY_SERVER="${EXECUTORCH_ARTIFACTORY_SERVER:-https://artifactory.arm.com:443}" \
    EXECUTORCH_ARTIFACTORY_REPOSITORY="${EXECUTORCH_ARTIFACTORY_REPOSITORY:-ai-expkits-internal.opk-deb}" \
    EXECUTORCH_ARTIFACTORY_DISTRIBUTION="${EXECUTORCH_ARTIFACTORY_DISTRIBUTION:-trixie}" \
    EXECUTORCH_ARTIFACTORY_COMPONENT="${EXECUTORCH_ARTIFACTORY_COMPONENT:-main}" \
    bash "$OPK_PROJECT_ROOT/scripts/private/executorch/install-executorch-deb.sh"

log "Installing actionlint ${ACTIONLINT_VERSION}"
temporary_directory="$(mktemp -d)"
trap 'rm -rf "${temporary_directory}"' EXIT
actionlint_archive="actionlint_${ACTIONLINT_VERSION}_linux_amd64.tar.gz"
actionlint_url="https://github.com/rhysd/actionlint/releases/download/v${ACTIONLINT_VERSION}"
curl --location -fsSLo "${temporary_directory}/${actionlint_archive}" \
    "${actionlint_url}/${actionlint_archive}"
curl --location -fsSLo "${temporary_directory}/checksums.txt" \
    "${actionlint_url}/actionlint_${ACTIONLINT_VERSION}_checksums.txt"
pushd "$temporary_directory" > /dev/null
grep " ${actionlint_archive}$" checksums.txt | sha256sum -c -
tar -xzf "$actionlint_archive" actionlint
popd > /dev/null
install -m 0755 "${temporary_directory}/actionlint" /usr/local/bin/actionlint

log "Installing the pinned web and Perception TypeScript tools"
readarray -t typescript_tools < <(
    node -e '
const path = require("path");
const projectRoot = process.env.OPK_PROJECT_ROOT;
const lock = require(path.join(projectRoot, "development/web/package-lock.json"));
const sdk = require(path.join(projectRoot, "tools/perception/sdk.json"));
console.log(lock.packages["node_modules/esbuild-wasm"].resolved);
console.log(lock.packages["node_modules/esbuild-wasm"].integrity);
console.log(sdk.typescript_build.flatbuffers_runtime.url);
console.log(sdk.typescript_build.flatbuffers_runtime.sha256);
console.log(sdk.typescript_build.typescript.url);
console.log(sdk.typescript_build.typescript.sha256);
'
)
esbuild_url="${typescript_tools[0]}"
esbuild_integrity="${typescript_tools[1]}"
flatbuffers_typescript_url="${typescript_tools[2]}"
flatbuffers_typescript_sha256="${typescript_tools[3]}"
typescript_url="${typescript_tools[4]}"
typescript_sha256="${typescript_tools[5]}"

download_npm_archive() {
    local url="$1"
    local destination="$2"

    timeout 180s curl \
        --fail --location --proto '=https' --proto-redir '=https' \
        --retry 1 --output "$destination" "$url" ||
        curl \
            --fail --location --proto '=https' --proto-redir '=https' \
            --retry 3 --output "$destination" \
            "${NPM_FALLBACK_REGISTRY}/${url#https://registry.npmjs.org/}"
}

download_npm_archive "$esbuild_url" "${temporary_directory}/esbuild-wasm.tgz"
download_npm_archive \
    "$flatbuffers_typescript_url" "${temporary_directory}/flatbuffers.tgz"
download_npm_archive "$typescript_url" "${temporary_directory}/typescript.tgz"
ESBUILD_INTEGRITY="$esbuild_integrity" node -e '
const crypto = require("crypto");
const fs = require("fs");
const [algorithm, expected] = process.env.ESBUILD_INTEGRITY.split("-", 2);
const actual = crypto.createHash(algorithm)
    .update(fs.readFileSync(process.argv[1]))
    .digest("base64");
if (actual !== expected) throw new Error("esbuild-wasm integrity mismatch");
' "${temporary_directory}/esbuild-wasm.tgz"
printf '%s  %s\n' \
    "$flatbuffers_typescript_sha256" "${temporary_directory}/flatbuffers.tgz" \
    "$typescript_sha256" "${temporary_directory}/typescript.tgz" |
    sha256sum --check --strict
npm install --global --ignore-scripts --no-audit --no-fund \
    "${temporary_directory}/esbuild-wasm.tgz" \
    "${temporary_directory}/flatbuffers.tgz" \
    "${temporary_directory}/typescript.tgz"

log "Installing uv and the OPK development-tool environment"
curl --proto '=https' --tlsv1.2 -LsSf \
    "https://releases.astral.sh/github/uv/releases/download/${UV_VERSION}/uv-installer.sh" |
    env UV_INSTALL_DIR=/usr/local/bin UV_NO_MODIFY_PATH=1 sh
uv pip install --system --break-system-packages -r "$OPK_PROJECT_ROOT/requirements/common.txt"
flatbuffers_wheel="$(python3 -c '
import json
import os
from pathlib import Path

descriptor = Path(os.environ["OPK_PROJECT_ROOT"]) / "tools/perception/sdk.json"
wheel = json.load(descriptor.open(encoding="utf-8"))["flatbuffers"]["python_wheel"]
print(wheel["url"] + "#sha256=" + wheel["sha256"])
')"
uv venv --clear --system-site-packages /opt/opk-venvs/devtools
uv pip install --python /opt/opk-venvs/devtools/bin/python \
    -c "$OPK_PROJECT_ROOT/requirements/sdk.txt" \
    "$OPK_PROJECT_ROOT/tools/opk-ci" \
    "$OPK_PROJECT_ROOT/generated/perception/python" \
    --editable "$OPK_PROJECT_ROOT/tools/plumber" \
    -r "$OPK_PROJECT_ROOT/requirements/models.txt" \
    -r "$OPK_PROJECT_ROOT/requirements/meson.txt" \
    "$flatbuffers_wheel"
env --chdir=/tmp \
    /opt/opk-venvs/devtools/bin/python -c 'import perception, plumber'
chown -R "$DEV_USER:$DEV_GROUP" /opt/opk-venvs/devtools

if [[ "$SKIP_SHELL" == false ]]; then
    log "Installing the pinned editor and debugger tools"
    curl --proto '=https' -fsSLo "${temporary_directory}/nvim.tar.gz" \
        "https://github.com/neovim/neovim/releases/download/${NVIM_VERSION}/nvim-linux-x86_64.tar.gz"
    rm -rf /opt/nvim
    install -d /opt/nvim
    tar -xzf "${temporary_directory}/nvim.tar.gz" --strip-components=1 -C /opt/nvim
    curl --proto '=https' -fsSLo "${temporary_directory}/cpptools.vsix" \
        "https://github.com/microsoft/vscode-cpptools/releases/download/${CPP_TOOLS_VERSION}/cpptools-linux-x64.vsix"
    rm -rf "${DEV_HOME}/bin/cpptools"
    install -d -o "$DEV_USER" -g "$DEV_GROUP" "${DEV_HOME}/bin/cpptools"
    unzip -q "${temporary_directory}/cpptools.vsix" -d "${DEV_HOME}/bin/cpptools"
    chmod +x "${DEV_HOME}/bin/cpptools/extension/debugAdapters/bin/OpenDebugAD7"
    chown -R "$DEV_USER:$DEV_GROUP" "${DEV_HOME}/bin"
    ln -sfn /opt/nvim/bin/nvim /usr/local/bin/nvim
    ln -sfn "${DEV_HOME}/bin/cpptools/extension/debugAdapters/bin/OpenDebugAD7" \
        /usr/local/bin/OpenDebugAD7
    update-alternatives --install /usr/bin/vi vi /usr/local/bin/nvim 60
    update-alternatives --install /usr/bin/vim vim /usr/local/bin/nvim 60
    update-alternatives --set vi /usr/local/bin/nvim
    update-alternatives --set vim /usr/local/bin/nvim
    luarocks install jsregexp
fi

log "Writing the OPK development environment"
{
    cat << 'EOF'
export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8
export GST_DEBUG="${GST_DEBUG:-2}"
EOF
    printf 'export OPK_PROJECT_ROOT=%q\n' "$OPK_PROJECT_ROOT"
    cat << 'EOF'
export GST_PLUGIN_PATH="$OPK_PROJECT_ROOT/development/build-active/meson-out${GST_PLUGIN_PATH:+:$GST_PLUGIN_PATH}"
export LD_LIBRARY_PATH="/opt/opk-deps/onnxruntime/lib:$OPK_PROJECT_ROOT/development/build-active/meson-out${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export OPK_DEVTOOLS_VENV=/opt/opk-venvs/devtools
export PATH="/opt/opk-venvs/devtools/bin:$PATH"
EOF
} > /etc/profile.d/opk-dev.sh
chmod 0644 /etc/profile.d/opk-dev.sh

# zsh does not automatically read /etc/profile.d. Source the shared definition
# from its system-wide startup file so login and non-login zsh shells agree.
install -d -m 0755 /etc/zsh
ln -sfn /etc/profile.d/opk-dev.sh /etc/zsh/opk-dev.zsh
if ! grep -Fqx 'source /etc/zsh/opk-dev.zsh' /etc/zsh/zshenv; then
    printf '\n# OPK development environment\nsource /etc/zsh/opk-dev.zsh\n' >> /etc/zsh/zshenv
fi

install -d -m 1777 /tmp
OPKCOMM_FIFO=/tmp/opkcomm
rm -f -- "$OPKCOMM_FIFO"
mkfifo --mode=0640 "$OPKCOMM_FIFO"
chown --no-dereference "$DEV_USER:$DEV_GROUP" "$OPKCOMM_FIFO"

if [[ "$SKIP_ASSETS" == false ]]; then
    log "Downloading model artifacts and demo videos as ${DEV_USER}"
    HF_TOKEN="$hf_token" \
        sudo --preserve-env=OPK_PROJECT_ROOT,HF_TOKEN -u "$DEV_USER" -H \
        /opt/opk-venvs/devtools/bin/python \
        "$OPK_PROJECT_ROOT/scripts/download-models.py" \
        --models-dir "$OPK_PROJECT_ROOT/config/models"
    sudo -u "$DEV_USER" -H \
        "$OPK_PROJECT_ROOT/scripts/private/download-demo-videos.sh"
fi
unset hf_token

log "Installing development hooks"
sudo -u "$DEV_USER" -H env OPK_PROJECT_ROOT="$OPK_PROJECT_ROOT" bash -lc \
    'cd "$OPK_PROJECT_ROOT" && /opt/opk-venvs/devtools/bin/pre-commit install && /opt/opk-venvs/devtools/bin/pre-commit install -t commit-msg'

log "LXC development environment is ready. Start a new login shell, then run:"
log "  cd $OPK_PROJECT_ROOT && ./scripts/build.sh debug false"
log "Group changes may require logging out of the LXC and back in."
