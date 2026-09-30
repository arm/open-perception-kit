#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Build the opk-runtime Debian package from a configured Meson build.

This private helper is an implementation detail of the Meson opk-runtime-deb target.
Use:

  meson compile -C <build-directory> opk-runtime-deb
EOF
}

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

need() {
    command -v "$1" > /dev/null 2>&1 || die "missing required command: $1"
}

resolve_existing_directory() {
    local path="$1"
    [[ -d "${path}" ]] || die "directory does not exist: ${path}"
    (cd -- "${path}" && pwd -P)
}

resolve_existing_file() {
    local path="$1"
    [[ -f "${path}" ]] || die "file does not exist: ${path}"
    local directory
    directory="$(cd -- "$(dirname -- "${path}")" && pwd -P)"
    printf '%s/%s\n' "${directory}" "$(basename -- "${path}")"
}

resolve_output() {
    local path="$1"
    local directory
    directory="$(dirname -- "${path}")"
    mkdir -p "${directory}"
    directory="$(cd -- "${directory}" && pwd -P)"
    printf '%s/%s\n' "${directory}" "$(basename -- "${path}")"
}

BUILD_DIR=""
MESON=""
REPO_ROOT=""
INSTALLED_PREFIX=""
INSTALLED_LIBDIR=""
PYTHON_RUNTIME=""
ONNX_RUNTIME=""
ONNX_LICENSE_DIR=""
EXECUTORCH_LICENSE_DIR=""
FLATBUFFERS_SCHEMA_DIR=""
VERSION=""
REVISION=""
ARCHITECTURE=""
MULTIARCH=""
OUTPUT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir | --meson | --repo-root | --installed-prefix | --installed-libdir | \
            --python-runtime | --onnx-runtime | --onnx-license-dir | \
            --executorch-license-dir | --version | \
            --flatbuffers-schema-dir | \
            --revision | --architecture | --multiarch | --output)
            [[ $# -ge 2 ]] || die "$1 requires a value"
            option="${1#--}"
            option="${option//-/_}"
            printf -v "${option^^}" '%s' "$2"
            shift 2
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

[[ "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
    die "version must be MAJOR.MINOR.PATCH: ${VERSION}"
[[ "${REVISION}" =~ ^[0-9A-Za-z.+~]+$ ]] ||
    die "invalid Debian revision: ${REVISION}"
case "${ARCHITECTURE}" in
    amd64 | arm64) ;;
    *) die "unsupported Debian architecture: ${ARCHITECTURE}" ;;
esac
case "${ARCHITECTURE}:${MULTIARCH}" in
    amd64:x86_64-linux-gnu | arm64:aarch64-linux-gnu) ;;
    *) die "invalid Debian architecture/multiarch pair: ${ARCHITECTURE}:${MULTIARCH}" ;;
esac
[[ "${INSTALLED_PREFIX}" == /* ]] ||
    die "installed prefix must be absolute: ${INSTALLED_PREFIX}"
[[ -n "${INSTALLED_LIBDIR}" ]] || die "installed libdir must not be empty"

BUILD_DIR="$(resolve_existing_directory "${BUILD_DIR}")"
REPO_ROOT="$(resolve_existing_directory "${REPO_ROOT}")"
DOCS_DIR="$(resolve_existing_directory "${REPO_ROOT}/docs")"
ONNX_RUNTIME="$(resolve_existing_file "${ONNX_RUNTIME}")"
ONNX_LICENSE_DIR="$(resolve_existing_directory "${ONNX_LICENSE_DIR}")"
EXECUTORCH_LICENSE_DIR="$(resolve_existing_directory "${EXECUTORCH_LICENSE_DIR}")"
FLATBUFFERS_SCHEMA_DIR="$(resolve_existing_directory "${FLATBUFFERS_SCHEMA_DIR}")"
OUTPUT="$(resolve_output "${OUTPUT}")"

[[ -x "${MESON}" ]] || die "Meson executable does not exist: ${MESON}"
[[ -x "${PYTHON_RUNTIME}" ]] || die "Python Ops runtime does not exist: ${PYTHON_RUNTIME}"
[[ -f "${REPO_ROOT}/scripts/release/ReleaseTool.py" ]] ||
    die "repository release tool is missing"

need dpkg-deb
need dpkg-shlibdeps
need find
need python3
need readelf

work_root="$(mktemp -d)"
trap 'rm -rf -- "${work_root}"' EXIT
install_root="${work_root}/install"
package_root="${work_root}/debian/opk-runtime"
package_lib_root="${package_root}/usr/lib/${MULTIARCH}"
plugin_root="${package_lib_root}/gstreamer-1.0"
private_root="${package_lib_root}/opk"
web_root="${package_root}/usr/share/opk/web"
onnx_license_root="${package_root}/usr/share/opk/licenses/onnxruntime"
executorch_license_root="${package_root}/usr/share/opk/licenses/executorch"
flatbuffers_schema_root="${package_root}/usr/share/opk/schemas/flatbuffers"
docs_root="${package_root}/usr/share/opk/docs"

mkdir -p "${install_root}"
DESTDIR="${install_root}" "${MESON}" install \
    -C "${BUILD_DIR}" --no-rebuild --skip-subprojects

if [[ "${INSTALLED_LIBDIR}" == /* ]]; then
    installed_root="${install_root}${INSTALLED_LIBDIR}"
else
    installed_root="${install_root}${INSTALLED_PREFIX%/}/${INSTALLED_LIBDIR}"
fi
installed_plugin_root="${installed_root}/gstreamer-1.0"
installed_private_root="${installed_root}/opk"

plugins=(
    libopkcomm.so
    libopkinfer.so
    libopkosd.so
    libopkperformance.so
    libopksink.so
    libopktracker.so
)
private_libraries=(
    libopk-common.so
    opk-runtime.so
    opk-executorch-ops.so
    opk-onnx-ops.so
    opk-python-ops.so
    opk-std-ops.so
)

mkdir -p "${package_root}/DEBIAN" "${plugin_root}" "${private_root}"
for library in "${plugins[@]}"; do
    [[ -f "${installed_plugin_root}/${library}" ]] ||
        die "Meson install is missing plugin: ${library}"
    install -m 0755 "${installed_plugin_root}/${library}" "${plugin_root}/${library}"
done

executorch_legal_files=(
    LICENSE
    GIT_COMMIT_ID
    VERSION_NUMBER
)
for legal_file in "${executorch_legal_files[@]}"; do
    [[ -f "${EXECUTORCH_LICENSE_DIR}/${legal_file}" ]] ||
        die "ExecuTorch legal or provenance file is missing: ${legal_file}"
done
[[ -n "$(find "${EXECUTORCH_LICENSE_DIR}/third-party" -type f -print -quit 2> /dev/null)" ]] ||
    die "ExecuTorch third-party legal documentation is missing"
[[ -z "$(find "${EXECUTORCH_LICENSE_DIR}" -type l -print -quit)" ]] ||
    die "ExecuTorch legal documentation must not contain symlinks"
mkdir -p "${executorch_license_root}"
cp -a "${EXECUTORCH_LICENSE_DIR}/." "${executorch_license_root}/"

mapfile -t flatbuffers_schemas < <(
    find "${FLATBUFFERS_SCHEMA_DIR}" -maxdepth 1 -type f -name '*.fbs' -print | sort
)
[[ ${#flatbuffers_schemas[@]} -gt 0 ]] ||
    die "Perception FlatBuffers schema directory contains no .fbs files"
[[ -z "$(find "${FLATBUFFERS_SCHEMA_DIR}" -maxdepth 1 -type l -print -quit)" ]] ||
    die "Perception FlatBuffers schema directory must not contain symlinks"
mkdir -p "${flatbuffers_schema_root}"
install -m 0644 "${flatbuffers_schemas[@]}" "${flatbuffers_schema_root}/"

[[ -z "$(find "${DOCS_DIR}" -type l -print -quit)" ]] ||
    die "OPK documentation must not contain symlinks"
mkdir -p "${docs_root}"
cp -a "${DOCS_DIR}/." "${docs_root}/"
find "${docs_root}" -type f -exec chmod 0644 {} +

installed_python_root="${install_root}${INSTALLED_PREFIX%/}/share/opk/python"
[[ -f "${installed_python_root}/opk_python_ops.pyi" ]] ||
    die "Meson install is missing the Python Ops type stub"
mkdir -p "${package_root}/usr/share/opk/python"
install -m 0644 "${installed_python_root}/opk_python_ops.pyi" \
    "${package_root}/usr/share/opk/python/opk_python_ops.pyi"
"${PYTHON_RUNTIME}" "${REPO_ROOT}/scripts/release/ReleaseTool.py" \
    stage-python-runtime --stage-root "${package_root}/usr" \
    --distribution open-perception-kit

runtime_architecture=x86_64
if [[ "${ARCHITECTURE}" == arm64 ]]; then
    runtime_architecture=aarch64
fi
mapfile -t python_dependency_lock < <(
    python3 -c \
        'import json, sys
runtime = json.load(open(sys.argv[1], encoding="utf-8"))
sdk = json.load(open(sys.argv[2], encoding="utf-8"))
numpy = runtime["numpy"]
numpy_wheel = numpy["wheels"][sys.argv[3]]
flatbuffers = sdk["flatbuffers"]
flatbuffers_wheel = flatbuffers["python_wheel"]
for value in (
    numpy["version"], numpy_wheel["url"], numpy_wheel["sha256"],
    flatbuffers["version"], flatbuffers_wheel["url"], flatbuffers_wheel["sha256"],
):
    print(value)' \
        "${REPO_ROOT}/development/ops-python/runtime.json" \
        "${REPO_ROOT}/tools/perception/sdk.json" \
        "${runtime_architecture}"
)
[[ ${#python_dependency_lock[@]} -eq 6 ]] ||
    die "invalid Python runtime dependency descriptors"
numpy_version="${python_dependency_lock[0]}"
numpy_wheel="${python_dependency_lock[1]}"
numpy_sha256="${python_dependency_lock[2]}"
flatbuffers_version="${python_dependency_lock[3]}"
flatbuffers_wheel="${python_dependency_lock[4]}"
flatbuffers_sha256="${python_dependency_lock[5]}"

cat > "${package_root}/DEBIAN/postinst" << EOF
#!/bin/sh
set -eu

if [ "\${1:-}" != configure ]; then
    exit 0
fi

target=/var/lib/opk/python
parent=\$(dirname "\${target}")
contract='numpy=${numpy_version}:${numpy_sha256};flatbuffers=${flatbuffers_version}:${flatbuffers_sha256}'
umask 022
mkdir -p -- "\${parent}"
if [ -f "\${target}/.opk-runtime-contract" ] && \
    [ "\$(cat "\${target}/.opk-runtime-contract")" = "\${contract}" ]; then
    echo "OPK Python runtime dependencies are already installed."
    exit 0
fi
temporary=\$(mktemp -d "\${parent}/.python.XXXXXX")
backup="\${parent}/.python.previous"
cleanup() {
    status=\$?
    trap - EXIT HUP INT TERM
    rm -rf -- "\${temporary}"
    if [ "\${status}" -ne 0 ] && [ ! -e "\${target}" ] && [ ! -L "\${target}" ] && \
        { [ -e "\${backup}" ] || [ -L "\${backup}" ]; }; then
        mv -- "\${backup}" "\${target}"
    fi
    if [ "\${status}" -ne 0 ]; then
        echo "ERROR: OPK Python dependencies could not be installed." >&2
        echo "Restore network access, then retry: sudo dpkg --configure opk-runtime" >&2
    fi
    exit "\${status}"
}
trap cleanup EXIT HUP INT TERM

if [ ! -e "\${target}" ] && [ ! -L "\${target}" ] && \
    { [ -e "\${backup}" ] || [ -L "\${backup}" ]; }; then
    mv -- "\${backup}" "\${target}"
fi

echo "Installing OPK Python runtime dependencies..."
/usr/bin/python3.13 -m pip install \
    --disable-pip-version-check \
    --no-cache-dir \
    --no-compile \
    --no-deps \
    --only-binary :all: \
    --target "\${temporary}" \
    '${numpy_wheel}#sha256=${numpy_sha256}' \
    '${flatbuffers_wheel}#sha256=${flatbuffers_sha256}'

/usr/bin/python3.13 - "\${temporary}" '${numpy_version}' '${flatbuffers_version}' << 'PYTHON'
import importlib
import importlib.metadata
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve()
expected = {"numpy": sys.argv[2], "flatbuffers": sys.argv[3]}
sys.path.insert(0, str(root))
for name, version in expected.items():
    module = importlib.import_module(name)
    actual = importlib.metadata.version(name)
    if actual != version:
        raise SystemExit(f"unexpected {name} version: {actual}; expected {version}")
    module_path = pathlib.Path(module.__file__).resolve()
    if not module_path.is_relative_to(root):
        raise SystemExit(f"{name} was loaded outside the OPK runtime: {module_path}")
PYTHON

printf '%s\n' "\${contract}" > "\${temporary}/.opk-runtime-contract"

rm -rf -- "\${backup}"
if [ -e "\${target}" ] || [ -L "\${target}" ]; then
    mv -- "\${target}" "\${backup}"
fi
if ! mv -- "\${temporary}" "\${target}"; then
    if [ -e "\${backup}" ] || [ -L "\${backup}" ]; then
        mv -- "\${backup}" "\${target}"
    fi
    exit 1
fi
rm -rf -- "\${backup}"
trap - EXIT HUP INT TERM
echo "OPK Python runtime dependencies are ready."
EOF

cat > "${package_root}/DEBIAN/postrm" << 'EOF'
#!/bin/sh
set -eu

if [ "${1:-}" = purge ]; then
    rm -rf -- /var/lib/opk/python /var/lib/opk/.python.previous
    rmdir --ignore-fail-on-non-empty /var/lib/opk 2> /dev/null || true
fi
EOF

installed_web_root="${install_root}${INSTALLED_PREFIX%/}/web/content"
[[ -f "${installed_web_root}/index.html" ]] ||
    die "Meson install is missing the opksink WebUI"
mkdir -p "${web_root}"
cp -a "${installed_web_root}/." "${web_root}/"
for library in "${private_libraries[@]}"; do
    [[ -f "${installed_private_root}/${library}" ]] ||
        die "Meson install is missing private library: ${library}"
    install -m 0755 "${installed_private_root}/${library}" "${private_root}/${library}"
done

install -m 0755 "${ONNX_RUNTIME}" "${private_root}/libonnxruntime.so.1.24.4"
ln -s libonnxruntime.so.1.24.4 "${private_root}/libonnxruntime.so.1"

onnx_legal_files=(
    LICENSE
    ThirdPartyNotices.txt
    GIT_COMMIT_ID
    VERSION_NUMBER
)
mkdir -p "${onnx_license_root}"
for legal_file in "${onnx_legal_files[@]}"; do
    [[ -f "${ONNX_LICENSE_DIR}/${legal_file}" ]] ||
        die "ONNX Runtime legal or provenance file is missing: ${legal_file}"
    install -m 0644 "${ONNX_LICENSE_DIR}/${legal_file}" \
        "${onnx_license_root}/${legal_file}"
done

expected_machine="Advanced Micro Devices X86-64"
if [[ "${ARCHITECTURE}" == arm64 ]]; then
    expected_machine="AArch64"
fi

elf_files=()
while IFS= read -r -d '' candidate; do
    if readelf -hW "${candidate}" > /dev/null 2>&1; then
        elf_files+=("${candidate}")
    fi
done < <(find "${package_root}/usr" -type f ! -type l -print0)
[[ ${#elf_files[@]} -gt 0 ]] || die "package contains no ELF files"

for binary in "${elf_files[@]}"; do
    machine="$(readelf -hW "${binary}" | sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p')"
    [[ "${machine}" == "${expected_machine}" ]] ||
        die "wrong ELF architecture in ${binary}: ${machine}"
done

[[ "$(readlink "${private_root}/libonnxruntime.so.1")" == libonnxruntime.so.1.24.4 ]] ||
    die "ONNX Runtime SONAME link is invalid"
[[ "$(readelf -dW "${private_root}/libonnxruntime.so.1.24.4" |
    sed -n 's/.*(SONAME).*\[\([^]]*\)\].*/\1/p')" == libonnxruntime.so.1 ]] ||
    die "ONNX Runtime has an unexpected SONAME"

for binary in "${plugin_root}"/*.so; do
    readelf -dW "${binary}" | grep -Fq "[\$ORIGIN/../opk]" ||
        die "plugin RUNPATH does not contain \$ORIGIN/../opk: ${binary}"
done
for binary in "${private_root}"/*.so*; do
    [[ -L "${binary}" ]] && continue
    if [[ "$(basename "${binary}")" == libonnxruntime.so.1.24.4 ]]; then
        continue
    fi
    readelf -dW "${binary}" | grep -Fq "[\$ORIGIN]" ||
        die "private library RUNPATH does not contain \$ORIGIN: ${binary}"
done

mkdir -p "${work_root}/debian"
cat > "${work_root}/debian/control" << EOF
Source: opk-runtime
Section: libs
Priority: optional
Maintainer: Arm Limited
Standards-Version: 4.7.0

Package: opk-runtime
Architecture: any
Description: Open Perception Kit GStreamer inference runtime
EOF

# The release build embeds the Python interpreter from /usr/local, so
# dpkg-shlibdeps cannot associate its SONAME with a Debian package through the
# dpkg database. The installed package deliberately uses Trixie's ABI-matching
# libpython instead.
cat > "${work_root}/debian/shlibs.local" << EOF
libpython3.13 1.0 libpython3.13 (>= 3.13)
EOF

declare -A shlib_lookup_directories_seen=()
shlib_lookup_arguments=()
for binary in "${elf_files[@]}"; do
    binary_directory="$(dirname -- "${binary}")"
    if [[ -z "${shlib_lookup_directories_seen[${binary_directory}]+x}" ]]; then
        shlib_lookup_directories_seen["${binary_directory}"]=1
        shlib_lookup_arguments+=("-l${binary_directory}")
    fi
done
shlibs_output="$(
    cd "${work_root}"
    dpkg-shlibdeps -O -xlibpython3.13 \
        "${shlib_lookup_arguments[@]}" "${elf_files[@]}"
)"
shlibs_depends="${shlibs_output#shlibs:Depends=}"
[[ "${shlibs_depends}" != "${shlibs_output}" && -n "${shlibs_depends}" ]] ||
    die "dpkg-shlibdeps did not produce shlibs:Depends"

runtime_depends=(
    ca-certificates
    gstreamer1.0-nice
    gstreamer1.0-plugins-bad
    gstreamer1.0-plugins-base
    gstreamer1.0-plugins-good
    libpython3.13
    python3.13
    python3-pip
)
for dependency in "${runtime_depends[@]}"; do
    shlibs_depends+=", ${dependency}"
done
if [[ "${ARCHITECTURE}" == arm64 ]]; then
    shlibs_depends+=", libusb-1.0-0"
fi

installed_size="$(du -sk "${package_root}/usr" | cut -f1)"
cat > "${package_root}/DEBIAN/control" << EOF
Package: opk-runtime
Version: ${VERSION}-${REVISION}
Section: libs
Priority: optional
Architecture: ${ARCHITECTURE}
Maintainer: Arm Limited
Installed-Size: ${installed_size}
Depends: ${shlibs_depends}
Recommends: gstreamer1.0-tools
Description: Open Perception Kit GStreamer inference runtime
 OPK GStreamer plugins, private ONNX and ExecuTorch operation modules,
 ONNX Runtime, and the generated Open Perception Kit Python package.
EOF

find "${package_root}" -type d -exec chmod 0755 {} +
chmod 0644 "${package_root}/DEBIAN/control"
chmod 0755 "${package_root}/DEBIAN/postinst" "${package_root}/DEBIAN/postrm"
dpkg-deb --root-owner-group --build "${package_root}" "${OUTPUT}"
printf 'Created %s\n' "${OUTPUT}"
