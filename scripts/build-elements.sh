#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

# ---- include ----
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
. "$SCRIPT_DIR/private/shtools.sh"

# ---- config ----
resolve_project_root() {
    local requested_root="${PEK_PROJECT_ROOT:-$SCRIPT_DIR/..}"

    if [[ "$requested_root" != /* ]]; then
        echo "PEK_PROJECT_ROOT must be an absolute path: $requested_root" >&2
        return 2
    fi

    if [[ ! -d "$requested_root" ]]; then
        echo "PEK project root does not exist: $requested_root" >&2
        return 2
    fi

    local resolved_root
    resolved_root="$(cd -- "$requested_root" && pwd -P)"
    if [[ ! -f "$resolved_root/development/meson.build" ]]; then
        echo "PEK project root has no development/meson.build: $resolved_root" >&2
        return 2
    fi

    printf '%s\n' "$resolved_root"
}

PEK_PROJECT_ROOT="$(resolve_project_root)"
export PEK_PROJECT_ROOT

MESON_SOURCE_ROOT="$PEK_PROJECT_ROOT/development"
if [[ "$PEK_PROJECT_ROOT" == "/work" ]]; then
    BUILD_DIR="$MESON_SOURCE_ROOT/build"
    TESTS_BUILD_DIR="$MESON_SOURCE_ROOT/build-test"
else
    # Meson build directories record absolute source paths and cannot be shared
    # between the /work container mount and a native host checkout.
    BUILD_DIR="$MESON_SOURCE_ROOT/build-native"
    TESTS_BUILD_DIR="$MESON_SOURCE_ROOT/build-native-test"
fi
ACTIVE_BUILD_DIR="$MESON_SOURCE_ROOT/build-active"
TOOLS_DIR="$PEK_PROJECT_ROOT/tools"
PEK_MENU="$BUILD_DIR/meson-out/pek-menu"
PEK_MENU_OUT="$TOOLS_DIR/pek-menu"
PEK_CONFIG_CHECK="$BUILD_DIR/meson-out/pek-config-check"
PEK_CONFIG_CHECK_OUT="$TOOLS_DIR/pek-config-check"
COMMON_LIBRARY="$BUILD_DIR/meson-out/libpek-common.so"
COMMON_LIBRARY_OUT="$TOOLS_DIR/libpek-common.so"
EXTRA_SETUP_ARGS=()
MESON_SETUP_ARGS=()
MESON_CONFIGURE_ARGS=()

meson_build_is_configured() {
    local build_dir="$1"
    [[ -d "$build_dir/meson-private" ]]
}

stage_runtime_artifacts() {
    mkdir -p "$TOOLS_DIR"
    cp "$PEK_MENU" "$PEK_MENU_OUT"
    cp "$PEK_CONFIG_CHECK" "$PEK_CONFIG_CHECK_OUT"
    cp "$COMMON_LIBRARY" "$COMMON_LIBRARY_OUT"
}

prepare_build_directory() {
    mkdir -p "$BUILD_DIR"
}

select_active_build_directory() {
    if [[ -e "$ACTIVE_BUILD_DIR" && ! -L "$ACTIVE_BUILD_DIR" ]]; then
        echo "Refusing to replace non-symlink active build path: $ACTIVE_BUILD_DIR" >&2
        return 2
    fi

    ln -sfn -- "$(basename -- "$BUILD_DIR")" "$ACTIVE_BUILD_DIR"
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
    MESON_CONFIGURE_ARGS+=("-D${option_name}=${normalized_value}")
    msg "Meson feature selection: ${option_name}=${normalized_value}"
}

collect_meson_args() {
    MESON_SETUP_ARGS=("${EXTRA_SETUP_ARGS[@]}")
    MESON_CONFIGURE_ARGS=("${EXTRA_SETUP_ARGS[@]}")

    # Meson owns the default (auto) for optional backends. Only pass a feature
    # option when the caller explicitly overrides it.
    add_feature_option_from_env "executorch" "PEK_EXECUTORCH"
    add_feature_option_from_env "hailort" "PEK_HAILORT"
    add_feature_option_from_env "ncnn" "PEK_NCNN"
}

# ---- build ----
debug() {
    need meson
    need ninja
    collect_meson_args

    local enable_tests="${1:-false}"

    prepare_build_directory
    msg "PEK project root: $PEK_PROJECT_ROOT"
    msg_begin "Starting DEBUG build in directory: $MESON_SOURCE_ROOT (tests=$enable_tests)"

    if ! meson_build_is_configured "$BUILD_DIR"; then
        msg "Meson setup.."
        meson setup "$BUILD_DIR" "$MESON_SOURCE_ROOT" --buildtype=debug --layout=flat -Dtests="$enable_tests" "${MESON_SETUP_ARGS[@]}"
    else
        msg "Meson configure (keeping existing build dir)…"
        meson configure "$BUILD_DIR" -Dtests="$enable_tests" "${MESON_CONFIGURE_ARGS[@]}" > /dev/null
    fi

    msg "Compiling.."
    meson compile -C "$BUILD_DIR"

    stage_runtime_artifacts
    select_active_build_directory

    msg_end "DEBUG compilation DONE → $BUILD_DIR"
}

release() {
    need meson
    need ninja
    collect_meson_args

    local enable_tests="${1:-false}"

    prepare_build_directory
    msg "PEK project root: $PEK_PROJECT_ROOT"
    msg_begin "Starting RELEASE build in directory: $MESON_SOURCE_ROOT (tests=$enable_tests)"

    if ! meson_build_is_configured "$BUILD_DIR"; then
        msg "Meson setup (release)…"
        meson setup "$BUILD_DIR" "$MESON_SOURCE_ROOT" \
            --buildtype=release \
            -Ddebug=false \
            -Dstrip=true \
            -Db_lto=true \
            -Doptimization=3 \
            --layout=flat \
            -Dtests="$enable_tests" \
            "${MESON_SETUP_ARGS[@]}"
    else
        msg "Meson configure (keeping existing build dir)…"
        meson configure "$BUILD_DIR" -Dtests="$enable_tests" "${MESON_CONFIGURE_ARGS[@]}" > /dev/null
    fi

    msg "Compiling…"
    meson compile -C "$BUILD_DIR"

    stage_runtime_artifacts
    select_active_build_directory

    msg_end "Release build done → $BUILD_DIR"
}
# ---- clean ----
clean() {
    local allowed_build_dir
    local active_build_target=""

    msg_begin "Executing CLEAN on $BUILD_DIR and $TESTS_BUILD_DIR"
    for allowed_build_dir in "$BUILD_DIR" "$TESTS_BUILD_DIR"; do
        case "$allowed_build_dir" in
            "$MESON_SOURCE_ROOT/build" | "$MESON_SOURCE_ROOT/build-test" | \
                "$MESON_SOURCE_ROOT/build-native" | \
                "$MESON_SOURCE_ROOT/build-native-test") ;;
            *)
                echo "Refusing to remove unexpected build directory: $allowed_build_dir" >&2
                exit 2
                ;;
        esac

        if [[ -d "$allowed_build_dir" ]]; then
            msg "REMOVING $allowed_build_dir…"
            rm -rf -- "$allowed_build_dir"
            msg_end "Done."
        else
            msg_end_err "no $allowed_build_dir to clean.."
        fi
    done

    if [[ -L "$ACTIVE_BUILD_DIR" ]]; then
        active_build_target="$(readlink "$ACTIVE_BUILD_DIR")"
        if [[ "$active_build_target" == "$(basename -- "$BUILD_DIR")" ||
              "$active_build_target" == "$(basename -- "$TESTS_BUILD_DIR")" ]]; then
            rm -f -- "$ACTIVE_BUILD_DIR"
        fi
    fi
}

# ---- help ----
usage() {
    cat << EOF

Commands:
  clean ➡️ Clear all build artifacts.
  debug [true|false] [--extra-setup-args=arg1,arg2=10] ➡️ Build elements in debug. Optional: enable/disable tests (default: false).
  release [true|false] [--extra-setup-args=arg1,arg2=10] ➡️ Build elements in release. Optional: enable/disable tests (default: false).

Optional backend feature environment variables:
  PEK_EXECUTORCH=enabled|disabled|auto  or  executorch=enabled|disabled|auto
  PEK_HAILORT=enabled|disabled|auto     or  hailort=enabled|disabled|auto
  PEK_NCNN=enabled|disabled|auto        or  ncnn=enabled|disabled|auto

Project location:
  PEK_PROJECT_ROOT=/absolute/path/to/amp-dev-forge
      Defaults to the repository containing this script. Inside the development
      container that remains /work.
      Docker builds use development/build; native builds use
      development/build-native so Meson's absolute source paths do not collide.
      Successful builds update development/build-active for editor tooling.

EOF
}

# ---- entrypoint ----
cmd="${1:-}"
if [[ $# -gt 0 ]]; then
    shift
fi
case "$cmd" in
    debug)
        parse_args true "$@"
        debug "${POSITIONAL_ARGS[0]:-false}"
        ;;
    release)
        parse_args true "$@"
        release "${POSITIONAL_ARGS[0]:-false}"
        ;;
    clean)
        parse_args false "$@"
        clean
        ;;
    *)
        echo "Unknown command: $cmd" >&2
        usage >&2
        exit 2
        ;;
esac
