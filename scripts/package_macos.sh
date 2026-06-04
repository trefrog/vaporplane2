#!/usr/bin/env bash
set -Eeuo pipefail

MODE=""
BUILD_DIR="${VAPORPLANE_BUILD_DIR:-build-macos-release}"
DIST_DIR="${VAPORPLANE_DIST_DIR:-dist}"
APP_NAME="${VAPORPLANE_APP_NAME:-Vaporplane}"
SIGN_IDENTITY="${VAPORPLANE_SIGN_IDENTITY:-}"
NOTARY_PROFILE="${VAPORPLANE_NOTARY_PROFILE:-}"
SDL3_DYLIB="${VAPORPLANE_SDL3_DYLIB:-}"
STARTER_SAMPLE="kmart1989_classy_pianist.wav"
STARTER_SAMPLE_JSON="kmart1989_classy_pianist.wav.json"

usage() {
    cat <<'USAGE'
Usage:
  scripts/package_macos.sh --local [options]
  scripts/package_macos.sh --developer-id --sign-identity ID [options]
  scripts/package_macos.sh --notarize --sign-identity ID --notary-profile PROFILE [options]

Options:
  --build-dir DIR         Release build directory (default: build-macos-release)
  --dist-dir DIR          Distribution output directory (default: dist)
  --sign-identity ID      Developer ID signing identity
  --notary-profile NAME   notarytool keychain profile name
  --sdl3-dylib PATH       SDL3 dylib to bundle instead of auto-detecting from otool
  -h, --help              Show this help

Environment alternatives:
  VAPORPLANE_BUILD_DIR
  VAPORPLANE_DIST_DIR
  VAPORPLANE_SIGN_IDENTITY
  VAPORPLANE_NOTARY_PROFILE
  VAPORPLANE_SDL3_DYLIB
USAGE
}

quote_cmd() {
    printf '+'
    printf ' %q' "$@"
    printf '\n'
}

run() {
    local status
    quote_cmd "$@"
    set +e
    "$@"
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf 'FAILED: ' >&2
        printf '%q ' "$@" >&2
        printf '(status %d)\n' "$status" >&2
        exit "$status"
    fi
}

run_capture() {
    local output status
    quote_cmd "$@" >&2
    set +e
    output="$("$@" 2>&1)"
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        printf '%s\n' "$output" >&2
        printf 'FAILED: ' >&2
        printf '%q ' "$@" >&2
        printf '(status %d)\n' "$status" >&2
        exit "$status"
    fi
    printf '%s' "$output"
}

fail() {
    printf 'FAILED: %s\n' "$*" >&2
    exit 1
}

make_abs_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$ROOT_DIR" "$1" ;;
    esac
}

step() {
    printf '\n==> %s\n' "$1"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --local)
            MODE="local"
            shift
            ;;
        --developer-id)
            MODE="developer-id"
            shift
            ;;
        --notarize)
            MODE="notarize"
            shift
            ;;
        --build-dir)
            [ "$#" -ge 2 ] || fail "--build-dir requires a value"
            BUILD_DIR="$2"
            shift 2
            ;;
        --dist-dir)
            [ "$#" -ge 2 ] || fail "--dist-dir requires a value"
            DIST_DIR="$2"
            shift 2
            ;;
        --sign-identity)
            [ "$#" -ge 2 ] || fail "--sign-identity requires a value"
            SIGN_IDENTITY="$2"
            shift 2
            ;;
        --notary-profile)
            [ "$#" -ge 2 ] || fail "--notary-profile requires a value"
            NOTARY_PROFILE="$2"
            shift 2
            ;;
        --sdl3-dylib)
            [ "$#" -ge 2 ] || fail "--sdl3-dylib requires a value"
            SDL3_DYLIB="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown argument: $1"
            ;;
    esac
done

[ -n "$MODE" ] || fail "choose one mode: --local, --developer-id, or --notarize"

if [ "$MODE" = "developer-id" ] || [ "$MODE" = "notarize" ]; then
    [ -n "$SIGN_IDENTITY" ] || fail "$MODE requires --sign-identity or VAPORPLANE_SIGN_IDENTITY"
fi

if [ "$MODE" = "notarize" ]; then
    [ -n "$NOTARY_PROFILE" ] || fail "--notarize requires --notary-profile or VAPORPLANE_NOTARY_PROFILE"
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR_ABS="$(make_abs_path "$BUILD_DIR")"
DIST_DIR_ABS="$(make_abs_path "$DIST_DIR")"
ARCH="$(uname -m)"
PACKAGE_DIR="$DIST_DIR_ABS/${APP_NAME}-macos-${ARCH}"
APP_DIR="$PACKAGE_DIR/$APP_NAME.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
FRAMEWORKS_DIR="$CONTENTS_DIR/Frameworks"
RESOURCES_DIR="$CONTENTS_DIR/Resources"
EXECUTABLE="$MACOS_DIR/vaporplane"
BUNDLED_SDL3="$FRAMEWORKS_DIR/libSDL3.0.dylib"
ZIP_PATH="$DIST_DIR_ABS/${APP_NAME}-macos-${ARCH}.zip"

[ "${APP_DIR##*/}" = "$APP_NAME.app" ] || fail "refusing to package into unexpected app path: $APP_DIR"
[ "${PACKAGE_DIR##*/}" = "${APP_NAME}-macos-${ARCH}" ] || fail "refusing to package into unexpected output path: $PACKAGE_DIR"

step "configure release build"
run cmake -S "$ROOT_DIR" -B "$BUILD_DIR_ABS" -DCMAKE_BUILD_TYPE=Release

step "build executable"
run cmake --build "$BUILD_DIR_ABS" --config Release --target vaporplane

step "create app bundle"
run rm -rf "$PACKAGE_DIR"
run mkdir -p "$MACOS_DIR" "$FRAMEWORKS_DIR" "$RESOURCES_DIR"

step "copy executable"
run cp "$BUILD_DIR_ABS/vaporplane" "$EXECUTABLE"
run chmod 755 "$EXECUTABLE"

step "locate/copy SDL3 dylib"
if [ -z "$SDL3_DYLIB" ]; then
    OTOOL_OUTPUT="$(run_capture otool -L "$EXECUTABLE")"
    SDL3_DYLIB="$(printf '%s\n' "$OTOOL_OUTPUT" | awk '/libSDL3.*\.dylib/ { print $1; exit }')"
fi
[ -n "$SDL3_DYLIB" ] || fail "could not locate SDL3 dylib dependency with otool"
[ -f "$SDL3_DYLIB" ] || fail "SDL3 dylib not found: $SDL3_DYLIB"
run cp "$SDL3_DYLIB" "$BUNDLED_SDL3"
run chmod 755 "$BUNDLED_SDL3"

step "fix install names"
run install_name_tool -id "@rpath/libSDL3.0.dylib" "$BUNDLED_SDL3"
run install_name_tool -change "$SDL3_DYLIB" "@rpath/libSDL3.0.dylib" "$EXECUTABLE"
run install_name_tool -add_rpath "@executable_path/../Frameworks" "$EXECUTABLE"

step "copy starter samples"
run mkdir -p "$RESOURCES_DIR/wav"
run cp "$ROOT_DIR/assets/samples/$STARTER_SAMPLE" "$RESOURCES_DIR/wav/$STARTER_SAMPLE"
run cp "$ROOT_DIR/assets/samples/$STARTER_SAMPLE_JSON" "$RESOURCES_DIR/wav/$STARTER_SAMPLE_JSON"

step "write tester quickstart"
run /usr/bin/env bash -c 'cat > "$1"' _ "$PACKAGE_DIR/README_FIRST.txt" <<'README'
Vaporplane Quickstart
=====================

First move:
- Press F1 in the app to show/hide the controls legend.
- Press F2 or R2+Start to cycle Waveform, Timeline, and Master Mix.
- The packaged starter sample is kmart1989_classy_pianist.

Waveform:
- Space or South/Start plays.
- A/D move loop start; J/L move loop end.
- T opens Tempo Lock when the loop needs BPM/downbeat calibration.
- L2+R2+South captures the current calibrated loop into the roster.

Timeline:
- Tab or bumpers cycle focus zones.
- Roster focus: South arms/places a captured clip.
- Track focus: South selects/moves clips; C or Start opens the context menu.
- R2+South plays/pauses, R2+East rewinds, R2+West sets playhead to the cursor, R2+North toggles play-range loop.

Drums:
- Select a drum lane, then use C/Start for drum pattern actions.
- South on a drum lane arms/places the selected pattern.
- Open the drum machine from the drum lane context menu.
- In the drum machine, D-pad moves, South toggles a step, bumpers change velocity, and East returns to the timeline.
- Drum kits load from assets/drum_packs in dev builds. Packaged tester builds may omit drum kits unless a package explicitly includes them.

Projects:
- Escape from idle Timeline opens the Project menu.
- Saved .vapor projects go under exports/projects.
README
run cp "$PACKAGE_DIR/README_FIRST.txt" "$RESOURCES_DIR/README_FIRST.txt"

step "write Info.plist"
run /usr/bin/env bash -c 'cat > "$1"' _ "$CONTENTS_DIR/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>vaporplane</string>
  <key>CFBundleIdentifier</key>
  <string>com.vaporplane.vaporplane</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>Vaporplane</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>0.1.0</string>
  <key>CFBundleVersion</key>
  <string>0.1.0</string>
  <key>LSMinimumSystemVersion</key>
  <string>12.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
</dict>
</plist>
PLIST

step "codesign"
if [ "$MODE" = "local" ]; then
    run codesign --force --deep --sign - "$APP_DIR"
else
    run codesign --force --deep --options runtime --timestamp --sign "$SIGN_IDENTITY" "$APP_DIR"
fi

step "verify"
run codesign --verify --deep --strict "$APP_DIR"
run otool -L "$EXECUTABLE"
run otool -D "$BUNDLED_SDL3"

step "zip"
run rm -f "$ZIP_PATH"
run ditto -c -k --keepParent "$PACKAGE_DIR" "$ZIP_PATH"

if [ "$MODE" = "notarize" ]; then
    step "notarize"
    run xcrun notarytool submit "$ZIP_PATH" --keychain-profile "$NOTARY_PROFILE" --wait
    run xcrun stapler staple "$APP_DIR"

    step "verify notarization"
    run spctl --assess --type execute --verbose "$APP_DIR"

    step "zip notarized app"
    run rm -f "$ZIP_PATH"
    run ditto -c -k --keepParent "$PACKAGE_DIR" "$ZIP_PATH"
fi

printf '\nPackaged: %s\n' "$ZIP_PATH"
