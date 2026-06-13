#!/usr/bin/env bash
set -Eeuo pipefail

BUILD_DIR="${VAPORPLANE_BUILD_DIR:-build-cloud}"
DEPS_DIR="${VAPORPLANE_DEPS_DIR:-.deps}"
SDL3_GIT_REF="${SDL3_GIT_REF:-release-3.2.0}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR_ABS="$ROOT_DIR/$BUILD_DIR"
DEPS_DIR_ABS="$ROOT_DIR/$DEPS_DIR"
SDL3_PREFIX="$DEPS_DIR_ABS/sdl3"
SDL3_SRC="$DEPS_DIR_ABS/src/SDL"
SDL3_BUILD="$DEPS_DIR_ABS/build/sdl3"

log() {
    printf '\n==> %s\n' "$1"
}

fail() {
    printf 'FAILED: %s\n' "$*" >&2
    exit 1
}

run() {
    printf '+'
    printf ' %q' "$@"
    printf '\n'
    "$@"
}

as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        run "$@"
    elif command -v sudo >/dev/null 2>&1; then
        run sudo "$@"
    else
        fail "need root privileges for: $*"
    fi
}

have_sdl3_cmake() {
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR_ABS/.probe-sdl3" \
        -DVAPORPLANE_ENABLE_SNDFILE=OFF \
        >/dev/null 2>&1
}

install_base_packages() {
    if command -v apt-get >/dev/null 2>&1; then
        log "install base build packages"
        as_root apt-get update
        as_root apt-get install -y --no-install-recommends \
            build-essential \
            ca-certificates \
            cmake \
            curl \
            git \
            ninja-build \
            pkg-config \
            libsndfile1-dev
    else
        log "skip apt packages"
        printf 'apt-get not found; assuming compiler, CMake, git, and pkg-config already exist.\n'
    fi
}

install_sdl3_from_apt_if_available() {
    command -v apt-get >/dev/null 2>&1 || return 1
    apt-cache show libsdl3-dev >/dev/null 2>&1 || return 1

    log "install SDL3 from apt"
    as_root apt-get install -y --no-install-recommends libsdl3-dev
}

install_sdl3_build_deps() {
    command -v apt-get >/dev/null 2>&1 || return 0

    log "install SDL3 source-build dependencies"
    as_root apt-get install -y --no-install-recommends \
        libasound2-dev \
        libdbus-1-dev \
        libegl1-mesa-dev \
        libgl1-mesa-dev \
        libpulse-dev \
        libudev-dev \
        libwayland-dev \
        libx11-dev \
        libxcursor-dev \
        libxext-dev \
        libxfixes-dev \
        libxi-dev \
        libxinerama-dev \
        libxkbcommon-dev \
        libxrandr-dev \
        wayland-protocols
}

install_sdl3_from_source() {
    log "build SDL3 from source ($SDL3_GIT_REF)"
    install_sdl3_build_deps
    run mkdir -p "$DEPS_DIR_ABS/src" "$DEPS_DIR_ABS/build"

    if [ -d "$SDL3_SRC/.git" ]; then
        run git -C "$SDL3_SRC" fetch --tags --prune
        run git -C "$SDL3_SRC" checkout "$SDL3_GIT_REF"
        run git -C "$SDL3_SRC" pull --ff-only || true
    else
        run git clone --depth 1 --branch "$SDL3_GIT_REF" \
            https://github.com/libsdl-org/SDL.git "$SDL3_SRC"
    fi

    run cmake -S "$SDL3_SRC" -B "$SDL3_BUILD" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$SDL3_PREFIX" \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TESTS=OFF
    run cmake --build "$SDL3_BUILD" --parallel
    run cmake --install "$SDL3_BUILD"
}

ensure_submodules() {
    log "initialize submodules"
    run git -C "$ROOT_DIR" submodule update --init --recursive
}

ensure_sdl3() {
    if have_sdl3_cmake; then
        log "SDL3 already available to CMake"
        return 0
    fi

    if install_sdl3_from_apt_if_available && have_sdl3_cmake; then
        return 0
    fi

    install_sdl3_from_source
}

configure_and_build() {
    local cmake_args=(
        -S "$ROOT_DIR"
        -B "$BUILD_DIR_ABS"
        -G Ninja
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
    )

    if [ -d "$SDL3_PREFIX/lib/cmake/SDL3" ]; then
        cmake_args+=("-DCMAKE_PREFIX_PATH=$SDL3_PREFIX")
    fi

    log "configure vaporplane"
    run cmake "${cmake_args[@]}"

    log "build vaporplane and tests"
    run cmake --build "$BUILD_DIR_ABS" --parallel

    log "run tests"
    run ctest --test-dir "$BUILD_DIR_ABS" --output-on-failure
}

cd "$ROOT_DIR"
install_base_packages
ensure_submodules
ensure_sdl3
configure_and_build

cat <<EOF

Cloud environment ready.

Useful commands:
  cmake --build $BUILD_DIR --parallel
  ctest --test-dir $BUILD_DIR --output-on-failure

If SDL3 source fetching fails, allow this setup step to reach:
  github.com

If submodule fetching fails, allow this setup step to reach:
  codeberg.org
EOF
