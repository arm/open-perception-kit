#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

# This private helper is invoked by the Meson opk-demo-deb target.

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

resolve_directory() {
    [[ -d "$1" ]] || die "directory does not exist: $1"
    (cd -- "$1" && pwd -P)
}

BUILD_DIR=""
MESON=""
REPO_ROOT=""
INSTALLED_PREFIX=""
VERSION=""
REVISION=""
ARCHITECTURE=""
OUTPUT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir | --meson | --repo-root | --installed-prefix | --version | \
            --revision | --architecture | --output)
            [[ $# -ge 2 ]] || die "$1 requires a value"
            option="${1#--}"
            option="${option//-/_}"
            printf -v "${option^^}" '%s' "$2"
            shift 2
            ;;
        *) die "unknown option: $1" ;;
    esac
done

[[ "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "invalid version: ${VERSION}"
[[ "${REVISION}" =~ ^[0-9A-Za-z.+~]+$ ]] || die "invalid revision: ${REVISION}"
case "${ARCHITECTURE}" in
    amd64) MULTIARCH=x86_64-linux-gnu ;;
    arm64) MULTIARCH=aarch64-linux-gnu ;;
    *) die "unsupported Debian architecture: ${ARCHITECTURE}" ;;
esac
[[ "${INSTALLED_PREFIX}" == /* ]] || die "installed prefix must be absolute"

BUILD_DIR="$(resolve_directory "${BUILD_DIR}")"
REPO_ROOT="$(resolve_directory "${REPO_ROOT}")"
[[ -x "${MESON}" ]] || die "Meson executable does not exist: ${MESON}"

for directory in config/models config/opchains config/pipelines config/schemas data/images data/videos; do
    [[ -d "${REPO_ROOT}/${directory}" ]] || die "required content is missing: ${directory}"
done

work_root="$(mktemp -d)"
trap 'rm -rf -- "${work_root}"' EXIT
install_root="${work_root}/install"
package_root="${work_root}/debian/opk-demo"
content_root="${package_root}/usr/share/opk"

mkdir -p "${install_root}" "${package_root}/DEBIAN" "${content_root}/config" \
    "${content_root}/data"
DESTDIR="${install_root}" "${MESON}" install \
    -C "${BUILD_DIR}" --no-rebuild --skip-subprojects

installed_launcher="${install_root}${INSTALLED_PREFIX%/}/bin/opk-menu"
[[ -x "${installed_launcher}" ]] || die "Meson install is missing opk-menu"
install -D -m 0755 "${installed_launcher}" "${package_root}/usr/bin/opk-menu"
expected_machine="Advanced Micro Devices X86-64"
if [[ "${ARCHITECTURE}" == arm64 ]]; then
    expected_machine="AArch64"
fi
machine="$(readelf -hW "${package_root}/usr/bin/opk-menu" |
    sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p')"
[[ "${machine}" == "${expected_machine}" ]] || die "opk-menu has wrong architecture: ${machine}"
readelf -dW "${package_root}/usr/bin/opk-menu" |
    grep -Fq "[\$ORIGIN/../lib/${MULTIARCH}/opk]" ||
    die "opk-menu RUNPATH does not locate the opk-runtime private libraries"

for directory in models opchains schemas; do
    cp -a "${REPO_ROOT}/config/${directory}" "${content_root}/config/${directory}"
done
mkdir -p "${content_root}/config/pipelines"
cp -a "${REPO_ROOT}/config/pipelines/." "${content_root}/config/pipelines/"
rm -f -- "${content_root}/config/pipelines/.last_selected_pipeline_id"
cp -a "${REPO_ROOT}/data/images" "${content_root}/data/images"
cp -a "${REPO_ROOT}/data/videos" "${content_root}/data/videos"

python3 - "${content_root}/config/models" << 'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
missing = []
for descriptor in sorted(root.rglob("model*.json")):
    document = json.loads(descriptor.read_text(encoding="utf-8"))
    model_file = document.get("modelFile")
    if isinstance(model_file, str) and not (descriptor.parent / model_file).is_file():
        missing.append(f"{descriptor}: {model_file}")
if missing:
    raise SystemExit("packaged model artifacts are missing:\n" + "\n".join(missing))
PY

installed_size="$(du -sk "${package_root}/usr" | cut -f1)"
cat > "${package_root}/DEBIAN/control" << EOF
Package: opk-demo
Version: ${VERSION}-${REVISION}
Section: misc
Priority: optional
Architecture: ${ARCHITECTURE}
Maintainer: Arm Limited
Installed-Size: ${installed_size}
Depends: opk-runtime (= ${VERSION}-${REVISION})
Description: Open Perception Kit runnable demonstration content
 OPK launcher, model descriptors and binaries, OpChains, pipeline presets,
 schemas, Python postprocessing scripts, and demonstration media.
EOF

find "${package_root}" -type d -exec chmod 0755 {} +
find "${package_root}/usr/share" -type f -exec chmod 0644 {} +
chmod 0644 "${package_root}/DEBIAN/control"
output_directory="$(dirname -- "${OUTPUT}")"
mkdir -p "${output_directory}"
OUTPUT="$(cd -- "${output_directory}" && pwd -P)/$(basename -- "${OUTPUT}")"
dpkg-deb --root-owner-group --build "${package_root}" "${OUTPUT}"
printf 'Created %s\n' "${OUTPUT}"
