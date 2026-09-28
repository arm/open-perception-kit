#!/usr/bin/env bash
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

set -euo pipefail

architecture="${1:?Target architecture is required (arm64)}"
archives_dir="/var/cache/apt/archives/${architecture}-sysroot"
sysroot="/opt/opk-sysroots/${architecture}"

if [[ "${architecture}" != arm64 ]]; then
    echo "Unsupported target architecture: ${architecture}. Expected arm64." >&2
    exit 1
fi

compiler_packages=(
    gcc-aarch64-linux-gnu
    g++-aarch64-linux-gnu
    binutils-aarch64-linux-gnu
)

dpkg --add-architecture "${architecture}"
apt-get update
apt-get install -y --no-install-recommends "${compiler_packages[@]}"

mkdir -p "${archives_dir}/partial" "${sysroot}"
ln -sfn usr/lib "${sysroot}/lib"
apt-get install -y --no-install-recommends --download-only \
    -o "Dir::Cache::archives=${archives_dir}" \
    "libssl-dev:${architecture}" \
    "libfmt-dev:${architecture}" \
    "libfftw3-dev:${architecture}" \
    "libsoup-3.0-dev:${architecture}" \
    "libjson-glib-dev:${architecture}" \
    "libgstreamer1.0-dev:${architecture}" \
    "libgstreamer-plugins-base1.0-dev:${architecture}" \
    "libgstreamer-plugins-bad1.0-dev:${architecture}"

find "${archives_dir}" -maxdepth 1 -type f -name "*.deb" -print0 |
    while IFS= read -r -d "" package; do
        case "$(dpkg-deb --field "${package}" Architecture)" in
            "${architecture}" | all)
                dpkg-deb --extract "${package}" "${sysroot}"
                ;;
        esac
    done

while read -r package; do
    dpkg-query --listfiles "${package}"
done < <(dpkg-query --show --showformat='${binary:Package} ${Architecture}\n' |
    awk '$2 == "all" { print $1 }') |
    sort --unique |
    while read -r path; do
        if [[ -f "${path}" || -L "${path}" ]]; then
            printf '%s\0' "${path#/}"
        fi
    done |
    tar --directory=/ --null --files-from=- --create --file=- |
    tar --extract --file=- --directory="${sysroot}"
