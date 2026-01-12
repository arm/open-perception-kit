#!/usr/bin/env bash

# ---- include ----
SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$SCRIPT_DIR/shtools.sh"

# ---- config ----
PROJECT_ROOT=/work/development
BUILD_DIR="$PROJECT_ROOT/build"
TESTS_BUILD_DIR="$PROJECT_ROOT/build-test"

# ---- build ----
debug() {
  need meson; need ninja

  local enable_tests="${1:-false}"

  msg_begin "Starting DEBUG build in directory: $PROJECT_ROOT (tests=$enable_tests)" 

  if [[ ! -d "$BUILD_DIR" ]]; then
    msg "Meson setup.."
    meson setup "$BUILD_DIR" "$PROJECT_ROOT" --buildtype=debug --layout=flat -Dtests="$enable_tests"
  else
    msg "Meson configure (keeping existing build dir)…"
    meson configure "$BUILD_DIR" >/dev/null
  fi

  msg "Compiling.."
  meson compile -C "$BUILD_DIR"

  msg_end "DEBUG compilation DONE → $BUILD_DIR"
}

debug_with_executorch() {
  need meson; need ninja

  msg_begin "Starting DEBUG build in directory: $PROJECT_ROOT" 

  if [[ ! -d "$BUILD_DIR" ]]; then
    msg "Meson setup.."
    meson setup "$BUILD_DIR" "$PROJECT_ROOT" --buildtype=debug --layout=flat --wrap-mode=forcefallback -Dexecutorch=enabled

  if [[ ! -d "$BUILD_DIR" ]]; then
    msg "Meson setup.."
    meson setup "$BUILD_DIR" "$PROJECT_ROOT" --buildtype=debug --layout=flat -Dtests="$enable_tests" --wrap-mode=forcefallback
  else
    msg "Meson configure (keeping existing build dir)…"
    meson configure "$BUILD_DIR" >/dev/null
  fi

  msg "Compiling.."
  meson compile -C "$BUILD_DIR"

  msg_end "DEBUG compilation DONE → $BUILD_DIR"
}

release() {
  need meson; need ninja

  local enable_tests="${1:-false}"

  msg_begin "Starting RELEASE build in directory: $PROJECT_ROOT (tests=$enable_tests)" 


  if [[ ! -d "$BUILD_DIR" ]]; then
    msg "Meson setup (release)…"
    meson setup "$BUILD_DIR" "$PROJECT_ROOT" \
      --buildtype=release  \
      -Ddebug=false \
      -Dstrip=true \
      -Db_lto=true \
      -Doptimization=3 \
      --layout=flat \
      -Dtests="$enable_tests"
  else
    msg "Meson configure (keeping existing build dir)…"
    meson configure "$BUILD_DIR" >/dev/null
  fi

  msg "Compiling…"
  meson compile -C "$BUILD_DIR"

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
  cat <<EOF

Commands:
  clean ➡️ Clear all build artifacts.
  debug [true|false] ➡️ Build elements in debug. Optional: enable/disable tests (default: false).
  release [true|false] ➡️ Build elements in release. Optional: enable/disable tests (default: false).

EOF
}

# ---- entrypoint ----
cmd="${1:-}"
arg="${2:-}"
case "$cmd" in
  debug) debug "$arg" ;;
  release) release "$arg" ;;
  debug_with_executorch) debug_with_executorch ;;
  clean) clean ;;
  *)
    echo "Unknown command: $cmd" >&2
    usage >&2
    exit 2
    ;;
esac