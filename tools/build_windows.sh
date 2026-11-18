#!/bin/sh
# Cross-compile the Windows build on Linux with MinGW-w64.
#
#   tools/build_windows.sh               build into build-windows/
#   tools/build_windows.sh test          ... and run the tests under wine
#   tools/build_windows.sh package       ... and zip it:
#       build-windows/tsw-re-<version>-windows-x86_64.zip
#
# Needs x86_64-w64-mingw32-gcc (Arch: mingw-w64-gcc), CMake, Ninja and curl;
# wine for the tests.  The SDL3 MinGW development releases are downloaded
# once into $TSW_MINGW_DEPS (default ~/.cache/tsw/mingw).
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEPS=${TSW_MINGW_DEPS:-${XDG_CACHE_HOME:-$HOME/.cache}/tsw/mingw}
BUILD=${TSW_WINDOWS_BUILD:-$ROOT/build-windows}

# repository package version
LIBS="SDL SDL3 3.4.20
SDL_image SDL3_image 3.4.8
SDL_ttf SDL3_ttf 3.2.2
SDL_mixer SDL3_mixer 3.2.4"

mkdir -p "$DEPS"
PREFIX=""
BIN=""
echo "$LIBS" | {
    while read -r repo pkg ver; do
        dir="$DEPS/$pkg-$ver"
        if [ ! -d "$dir" ]; then
            tar="$DEPS/$pkg-devel-$ver-mingw.tar.gz"
            url="https://github.com/libsdl-org/$repo/releases/download/release-$ver/$pkg-devel-$ver-mingw.tar.gz"
            echo "downloading $url"
            curl -fL --proto '=https' -o "$tar" "$url"
            tar -xzf "$tar" -C "$DEPS"
            rm -f "$tar"
        fi
        PREFIX="$PREFIX${PREFIX:+;}$dir/x86_64-w64-mingw32"
        BIN="$BIN${BIN:+;}$dir/x86_64-w64-mingw32/bin"
    done
    printf '%s\n%s\n' "$PREFIX" "$BIN" > "$DEPS/paths"
}
PREFIX=$(sed -n 1p "$DEPS/paths")
BIN=$(sed -n 2p "$DEPS/paths")

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64-x86_64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_FIND_ROOT_PATH="$PREFIX"
cmake --build "$BUILD"

for step in "$@"; do
    case $step in
    test)
        # The SDL DLLs for the tests; the package carries its own copies.
        WINEPATH="$BIN" WINEDEBUG=-all TSW_NO_AUDIO=1 SDL_VIDEODRIVER=dummy \
            ctest --test-dir "$BUILD" --output-on-failure
        ;;
    package)
        (cd "$BUILD" && cpack --config CPackConfig.cmake)
        ;;
    *)
        echo "unknown step: $step" >&2
        exit 2
        ;;
    esac
done
