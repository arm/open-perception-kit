#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  ./scripts/build.sh [debug|release|clean] [build options]
  ./scripts/build.sh [-h|--help]

With no arguments, builds PEK in debug mode. Inside a container, this command
runs the Meson build directly. On a host, it starts the matching quick-start
container when needed and runs the same command there.

Commands:
  clean
      Clear all build artifacts.
  debug [true|false] [--extra-setup-args=arg1,arg2=10]
      Build in debug mode. Tests default to false.
  release [true|false] [--extra-setup-args=arg1,arg2=10]
      Build in release mode. Tests default to false.

Optional backend feature environment variables:
  PEK_EXECUTORCH=enabled|disabled|auto  or  executorch=enabled|disabled|auto
  PEK_HAILORT=enabled|disabled|auto     or  hailort=enabled|disabled|auto
  PEK_NCNN=enabled|disabled|auto        or  ncnn=enabled|disabled|auto
EOF
}

SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

running_in_container() {
    [[ "$REPO_ROOT" == /work || -f /.dockerenv ]] ||
        grep -qaE '/docker/|/containers/' /proc/1/cgroup 2> /dev/null
}

run_on_host() {
    local detect_script="$REPO_ROOT/scripts/quick-start/detect-environment.sh"
    local start_container_script="$REPO_ROOT/scripts/quick-start/start-container.sh"
    local detect_output
    local -a docker_exec_args

    if ! detect_output="$("$detect_script" --shell)"; then
        eval "$detect_output"
        echo "Error: unsupported quick-start platform: ${PEK_PLATFORM_NAME:-unknown}" >&2
        if [[ -n "${PEK_UNSUPPORTED_REASON:-}" ]]; then
            echo "Reason: ${PEK_UNSUPPORTED_REASON}" >&2
        fi
        exit 1
    fi
    eval "$detect_output"

    cd "$REPO_ROOT"
    "$start_container_script"

    docker_exec_args=(-u dev)
    if [[ -f "$REPO_ROOT/devices.env" ]]; then
        docker_exec_args+=(--env-file "$REPO_ROOT/devices.env")
    fi
    for env_name in PEK_EXECUTORCH PEK_HAILORT PEK_NCNN executorch hailort ncnn; do
        if [[ "${!env_name+x}" == x ]]; then
            docker_exec_args+=(--env "$env_name=${!env_name}")
        fi
    done
    docker exec "${docker_exec_args[@]}" "$PEK_CONTAINER_NAME" \
        bash -lc 'cd /work && ./scripts/build.sh "$@"' bash "$@"

    if [[ "$1" != clean ]]; then
        docker exec -u dev "$PEK_CONTAINER_NAME" test -x /work/tools/pek-menu
        echo "Pipeline launcher is ready at /work/tools/pek-menu"
    fi
}

# ---- config ----
MESON_SOURCE_DIR=/work/development
BUILD_DIR="$MESON_SOURCE_DIR/build"
TESTS_BUILD_DIR="$MESON_SOURCE_DIR/build-test"
PEK_MENU=$MESON_SOURCE_DIR/build/meson-out/pek-menu
PEK_MENU_OUT=/work/tools/pek-menu
PEK_CONFIG_CHECK=$MESON_SOURCE_DIR/build/meson-out/pek-config-check
PEK_CONFIG_CHECK_OUT=/work/tools/pek-config-check
COMMON_LIBRARY=$MESON_SOURCE_DIR/build/meson-out/libpek-common.so
COMMON_LIBRARY_OUT=/work/tools/libpek-common.so
COMMAND=""
BUILD_LABEL=""
EXTRA_SETUP_ARGS=()
MESON_SETUP_ARGS=()
MESON_MODE_ARGS=()
POSITIONAL_ARGS=()

meson_build_is_configured() {
    local build_dir="$1"
    [[ -d "$build_dir/meson-private" ]]
}

stage_runtime_artifacts() {
    cp "$PEK_MENU" "$PEK_MENU_OUT"
    cp "$PEK_CONFIG_CHECK" "$PEK_CONFIG_CHECK_OUT"
    cp "$COMMON_LIBRARY" "$COMMON_LIBRARY_OUT"
}

parse_extra_setup_args() {
    local raw_args="$1"

    EXTRA_SETUP_ARGS=()
    if [[ -z "$raw_args" ]]; then
        return
    fi

    local IFS=,
    read -r -a EXTRA_SETUP_ARGS <<< "$raw_args"
}

parse_args() {
    local allow_tests_arg="$1"
    shift

    POSITIONAL_ARGS=()

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --extra-setup-args=*)
                parse_extra_setup_args "${1#--extra-setup-args=}"
                ;;
            --extra-setup-args)
                echo "--extra-setup-args requires the form --extra-setup-args=arg1,arg2=10" >&2
                usage >&2
                exit 2
                ;;
            --*)
                echo "Unknown option: $1" >&2
                usage >&2
                exit 2
                ;;
            *)
                POSITIONAL_ARGS+=("$1")
                ;;
        esac
        shift
    done

    if [[ "$allow_tests_arg" == "true" ]]; then
        if [[ ${#POSITIONAL_ARGS[@]} -gt 1 ]]; then
            echo "Too many arguments: ${POSITIONAL_ARGS[*]}" >&2
            usage >&2
            exit 2
        fi
    elif [[ ${#POSITIONAL_ARGS[@]} -gt 0 ]]; then
        echo "Unexpected arguments: ${POSITIONAL_ARGS[*]}" >&2
        usage >&2
        exit 2
    fi
}

parse_command() {
    COMMAND="$1"
    shift
    EXTRA_SETUP_ARGS=()

    case "$COMMAND" in
        debug)
            parse_args true "$@"
            BUILD_LABEL="DEBUG"
            MESON_MODE_ARGS=(
                --buildtype=debug
                -Ddebug=true
                -Dstrip=false
                -Db_lto=false
                -Doptimization=0
            )
            ;;
        release)
            parse_args true "$@"
            BUILD_LABEL="RELEASE"
            MESON_MODE_ARGS=(
                --buildtype=release
                -Ddebug=false
                -Dstrip=true
                -Db_lto=true
                -Doptimization=3
            )
            ;;
        clean)
            parse_args false "$@"
            ;;
        *)
            echo "Unknown command: $COMMAND" >&2
            usage >&2
            exit 2
            ;;
    esac
}

normalize_feature_value() {
    local name="$1"
    local value="$2"

    case "$value" in
        enabled | enable | true | 1 | yes | on)
            printf "enabled"
            ;;
        disabled | disable | false | 0 | no | off)
            printf "disabled"
            ;;
        auto)
            printf "auto"
            ;;
        *)
            echo "Invalid value for $name: $value (expected enabled, disabled, or auto)" >&2
            exit 2
            ;;
    esac
}

add_feature_option_from_env() {
    local option_name="$1"
    local env_name="$2"
    local default_value="${3:-}"
    local raw_value="${!env_name:-}"
    local short_value="${!option_name:-}"
    local normalized_value

    if [[ -n "$short_value" ]]; then
        raw_value="$short_value"
    fi

    if [[ -z "$raw_value" ]]; then
        local arg
        for arg in "${EXTRA_SETUP_ARGS[@]}"; do
            case "$arg" in
                "-D${option_name}="*) return 0 ;;
            esac
        done
    fi

    raw_value="${raw_value:-$default_value}"
    [[ -n "$raw_value" ]] || return 0

    normalized_value="$(normalize_feature_value "$env_name/$option_name" "$raw_value")"
    MESON_SETUP_ARGS+=("-D${option_name}=${normalized_value}")
    msg "Meson feature selection: ${option_name}=${normalized_value}"
}

collect_meson_args() {
    MESON_SETUP_ARGS=("${EXTRA_SETUP_ARGS[@]}")

    add_feature_option_from_env "executorch" "PEK_EXECUTORCH" "auto"
    add_feature_option_from_env "hailort" "PEK_HAILORT"
    add_feature_option_from_env "ncnn" "PEK_NCNN"
}

# ---- build ----
build() {
    need meson
    need ninja

    local enable_tests="${1:-false}"
    local arg
    local -a reuse_args=()

    if meson_build_is_configured "$BUILD_DIR"; then
        reuse_args=(--reconfigure)
        for arg in "${MESON_SETUP_ARGS[@]}"; do
            if [[ "$arg" == --wipe ]]; then
                reuse_args=()
                break
            fi
        done
    fi

    msg_begin "Starting $BUILD_LABEL build in directory: $MESON_SOURCE_DIR (tests=$enable_tests)"
    msg "Meson setup…"
    meson setup "$BUILD_DIR" "$MESON_SOURCE_DIR" \
        "${reuse_args[@]}" \
        "${MESON_MODE_ARGS[@]}" \
        --layout=flat \
        -Dtests="$enable_tests" \
        "${MESON_SETUP_ARGS[@]}"

    msg "Compiling…"
    meson compile -C "$BUILD_DIR"

    stage_runtime_artifacts

    msg_end "$BUILD_LABEL build done → $BUILD_DIR"
}
# ---- clean ----
clean() {
    msg_begin "Executing CLEAN on $BUILD_DIR and $TESTS_BUILD_DIR"
    if [[ -d "$BUILD_DIR" ]]; then
        msg "REMOVING $BUILD_DIR…"
        rm -rf "$BUILD_DIR"
        msg_end "Done."
    else
        msg_end_err "no $BUILD_DIR to clean.."
    fi
    if [[ -d "$TESTS_BUILD_DIR" ]]; then
        msg "REMOVING $TESTS_BUILD_DIR"
        rm -rf "$TESTS_BUILD_DIR"
        msg_end "Done."
    else
        msg_end_err "no $TESTS_BUILD_DIR to clean.."
    fi
}

# ---- entrypoint ----
main() {
    if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
        usage
        return
    fi

    if [[ $# -eq 0 ]]; then
        set -- debug
    fi

    parse_command "$@"

    if ! running_in_container; then
        run_on_host "$@"
        return
    fi

    # ---- include ----
    . "$SCRIPT_DIR/private/shtools.sh"

    mkdir -p "$BUILD_DIR"
    if [[ "$COMMAND" == clean ]]; then
        clean
        return
    fi

    collect_meson_args
    build "${POSITIONAL_ARGS[0]:-false}"
}

main "$@"
