#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

# ---- include ----
SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$SCRIPT_DIR/private/shtools.sh"

# ---- config ----
PROJECT_ROOT=/work/development
BUILD_DIR="$PROJECT_ROOT/build"
TESTS_BUILD_DIR="$PROJECT_ROOT/build-test"
PEK_MENU=$PROJECT_ROOT/build/meson-out/pek-menu
PEK_MENU_OUT=/work/tools/pek-menu
EXTRA_SETUP_ARGS=()

mkdir -p "$BUILD_DIR"

meson_build_is_configured() {
    local build_dir="$1"
    [[ -d "$build_dir/meson-private" ]]
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

# ---- build ----
debug() {
    need meson
    need ninja

    local enable_tests="${1:-false}"

    msg_begin "Starting DEBUG build in directory: $PROJECT_ROOT (tests=$enable_tests)"

    if ! meson_build_is_configured "$BUILD_DIR"; then
        msg "Meson setup.."
        meson setup "$BUILD_DIR" "$PROJECT_ROOT" --buildtype=debug --layout=flat -Dtests="$enable_tests" "${EXTRA_SETUP_ARGS[@]}"
    else
        msg "Meson configure (keeping existing build dir)…"
        meson configure "$BUILD_DIR" > /dev/null
    fi

    msg "Compiling.."
    meson compile -C "$BUILD_DIR"

    cp "$PEK_MENU" "$PEK_MENU_OUT"

    msg_end "DEBUG compilation DONE → $BUILD_DIR"
}

debug_with_executorch() {
    need meson
    need ninja

    msg_begin "Starting DEBUG build with ExecuTorch in directory: $PROJECT_ROOT"

    if ! meson_build_is_configured "$BUILD_DIR"; then
        msg "Meson setup.."
        meson setup "$BUILD_DIR" "$PROJECT_ROOT" --buildtype=debug --layout=flat --wrap-mode=forcefallback -Dexecutorch=enabled "${EXTRA_SETUP_ARGS[@]}"
    else
        msg "Meson configure (keeping existing build dir)…"
        meson configure "$BUILD_DIR" > /dev/null
    fi

    msg "Compiling.."
    meson compile -C "$BUILD_DIR"

    cp "$PEK_MENU" "$PEK_MENU_OUT"

    msg_end "DEBUG compilation with ExecuTorch DONE → $BUILD_DIR"
}

release() {
    need meson
    need ninja

    local enable_tests="${1:-false}"

    msg_begin "Starting RELEASE build in directory: $PROJECT_ROOT (tests=$enable_tests)"

    if ! meson_build_is_configured "$BUILD_DIR"; then
        msg "Meson setup (release)…"
        meson setup "$BUILD_DIR" "$PROJECT_ROOT" \
            --buildtype=release \
            -Ddebug=false \
            -Dstrip=true \
            -Db_lto=true \
            -Doptimization=3 \
            --layout=flat \
            -Dtests="$enable_tests" \
            "${EXTRA_SETUP_ARGS[@]}"
    else
        msg "Meson configure (keeping existing build dir)…"
        meson configure "$BUILD_DIR" > /dev/null
    fi

    msg "Compiling…"
    meson compile -C "$BUILD_DIR"

    cp "$PEK_MENU" "$PEK_MENU_OUT"

    msg_end "Release build done → $BUILD_DIR"
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

# ---- help ----
usage() {
    cat << EOF

Commands:
  clean ➡️ Clear all build artifacts.
  debug [true|false] [--extra-setup-args=arg1,arg2=10] ➡️ Build elements in debug. Optional: enable/disable tests (default: false).
  debug_with_executorch [--extra-setup-args=arg1,arg2=10] ➡️ Build elements in debug with ExecuTorch.
  release [true|false] [--extra-setup-args=arg1,arg2=10] ➡️ Build elements in release. Optional: enable/disable tests (default: false).

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
    debug_with_executorch)
        parse_args false "$@"
        debug_with_executorch
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
