#!/usr/bin/env bash
# Build SDL3, SDL3_image and SDL3_ttf from source into a private prefix.
#
# For machines without system SDL3 packages (CI, containers, sandboxed
# agent VMs such as Ubuntu 22.04).  By default SDL is built "headless": no
# X11/Wayland/KMS/audio backends, which is enough for ctest (test_input runs
# with SDL_VIDEODRIVER=dummy) and for `tsw-re --screenshot`.  Pass
# --desktop to keep SDL's default window/audio backends (needs their -dev
# packages).
#
# Requirements: a C compiler, CMake >= 3.16 and Ninja (for example
# `pip install --user cmake ninja`), curl, tar, network access to GitHub,
# and the FreeType development files for SDL3_ttf.
#
# Usage:
#   tools/build_sdl3_headless.sh [--desktop] [PREFIX]   # default ~/opt/sdl3
#
# Then build and test tsw-re against it:
#   cmake -S . -B build-sdl -G Ninja -DCMAKE_PREFIX_PATH=~/opt/sdl3
#   cmake --build build-sdl
#   LD_LIBRARY_PATH=~/opt/sdl3/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
#       ctest --test-dir build-sdl --output-on-failure

set -euo pipefail

SDL_VERSION=${SDL_VERSION:-3.4.20}
SDL_IMAGE_VERSION=${SDL_IMAGE_VERSION:-3.4.8}
SDL_TTF_VERSION=${SDL_TTF_VERSION:-3.2.2}

desktop=0
if [[ ${1:-} == --desktop ]]; then
    desktop=1
    shift
fi
prefix=${1:-$HOME/opt/sdl3}
src=${SDL_SRC_DIR:-$HOME/.cache/tsw/sdl3-src}
mkdir -p "$src" "$prefix"

fetch() {  # repo tarball
    local url="https://github.com/libsdl-org/$1/releases/download/release-${2#*-}/$2.tar.gz"
    if [[ ! -d $src/$2 ]]; then
        echo "fetching $url"
        curl -fsSL "$url" | tar xz -C "$src"
    fi
}

fetch SDL "SDL3-$SDL_VERSION"
fetch SDL_image "SDL3_image-$SDL_IMAGE_VERSION"
fetch SDL_ttf "SDL3_ttf-$SDL_TTF_VERSION"

sdl_opts=(-DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF)
if (( ! desktop )); then
    sdl_opts+=(-DSDL_UNIX_CONSOLE_BUILD=ON
               -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF
               -DSDL_ALSA=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF
               -DSDL_JACK=OFF -DSDL_SNDIO=OFF
               -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF
               -DSDL_GPU=OFF -DSDL_CAMERA=OFF -DSDL_HIDAPI=OFF
               -DSDL_DBUS=OFF -DSDL_IBUS=OFF)
fi

build() {  # srcdir builddir options...
    local s=$1 b=$2
    shift 2
    cmake -S "$src/$s" -B "$src/$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" "$@"
    cmake --build "$src/$b"
    cmake --install "$src/$b"
}

build "SDL3-$SDL_VERSION" b-sdl "${sdl_opts[@]}"
# PNG/BMP through SDL_image's built-in stb backend; no optional codecs.
build "SDL3_image-$SDL_IMAGE_VERSION" b-img \
    -DSDLIMAGE_VENDORED=OFF -DSDLIMAGE_DEPS_SHARED=OFF \
    -DSDLIMAGE_AVIF=OFF -DSDLIMAGE_JXL=OFF -DSDLIMAGE_TIF=OFF \
    -DSDLIMAGE_WEBP=OFF -DSDLIMAGE_SAMPLES=OFF -DSDLIMAGE_TESTS=OFF
build "SDL3_ttf-$SDL_TTF_VERSION" b-ttf \
    -DSDLTTF_VENDORED=OFF -DSDLTTF_HARFBUZZ=OFF -DSDLTTF_PLUTOSVG=OFF \
    -DSDLTTF_SAMPLES=OFF

echo "SDL3 installed in $prefix"
