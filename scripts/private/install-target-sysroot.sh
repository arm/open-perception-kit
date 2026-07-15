#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

architecture="${1:?Target architecture is required (amd64 or arm64)}"
archives_dir="/var/cache/apt/archives/${architecture}-sysroot"
sysroot="/opt/pek-sysroots/${architecture}"

case "${architecture}" in
    amd64)
        compiler_packages=(
            gcc-x86-64-linux-gnu
            g++-x86-64-linux-gnu
            binutils-x86-64-linux-gnu
        )
        triplet=x86_64-linux-gnu
        ;;
    arm64)
        compiler_packages=(
            gcc-aarch64-linux-gnu
            g++-aarch64-linux-gnu
            binutils-aarch64-linux-gnu
        )
        triplet=aarch64-linux-gnu
        ;;
    *)
        echo "Unsupported target architecture: ${architecture}" >&2
        exit 1
        ;;
esac

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
    "libcairo2-dev:${architecture}" \
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
