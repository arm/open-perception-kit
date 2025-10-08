#!/usr/bin/env bash

# ---- include ----
SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$SCRIPT_DIR/shtools.sh"

# ---- config ----
PROJECT_ROOT=/work/development
BUILD_DIR="$PROJECT_ROOT/build"

# ---- build ----
debug() {
  need meson; need ninja

  msg_begin "Starting DEBUG build in directory: $PROJECT_ROOT" 

  if [[ ! -d "$BUILD_DIR" ]]; then
    msg "Meson setup.."
    meson setup "$BUILD_DIR" "$PROJECT_ROOT" --buildtype=debug --layout=flat
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

  msg_begin "Starting RELEASE build in directory: $PROJECT_ROOT" 


  if [[ ! -d "$BUILD_DIR" ]]; then
    msg "Meson setup (release)…"
    meson setup "$BUILD_DIR" "$PROJECT_ROOT" \
      --buildtype=release  \
      -Ddebug=false \
      -Dstrip=true \
      -Db_lto=true \
      -Doptimization=3 \
      --layout=flat
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
    msg_begin "Executing CLEAN on $BUILD_DIR"
  if [[ -d "$BUILD_DIR" ]]; then
    msg "REMOVING $BUILD_DIR…"
    rm -rf "$BUILD_DIR"
    msg_end "Done."
  else
    msg_end_err "NOTHING to clean.."
  fi
}

# ---- help ----
usage() {
  cat <<EOF

Commands:
  clean ➡️ Clear all build artifacts.
  debug ➡️ Build elements in debug.
  release ➡️ Build elements in release.

EOF
}

# ---- entrypoint ----
cmd="${1:-}"
case "$cmd" in
  debug) debug ;;
  release) release ;;
  clean) clean ;;
  *)
    echo "Unknown command: $cmd" >&2
    usage >&2
    exit 2
    ;;
esac