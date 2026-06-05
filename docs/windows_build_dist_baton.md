# Windows Build And Distribution Baton Pass

## Summary

Vaporplane's supported local Windows build and distribution path is now
MSYS2/MINGW64. The Visual Studio/vcpkg route was attempted and is not the local
tester distribution target.

Windows packaging remains explicit. Ordinary development builds should stay
plain:

```text
cmake --build build
```

Distribution work happens through `scripts/package_windows.ps1`.

## Current State

- The app builds with CMake, C11/C++17, SDL3, and bundled SoundTouch.
- `CMakeLists.txt` links `vaporplane` against `SDL3::SDL3` and `SoundTouch`.
- SoundTouch is built from `third_party/soundtouch` with `SOUNDTOUCH_DLL OFF`,
  so the project expects SoundTouch to be statically linked into the executable.
- `scripts/build-win-mingw.cmd` is a repo-relative Debug helper for MSYS2 at
  `C:\msys64`.
- `scripts/package_windows.ps1` is the Windows distribution entrypoint.
- GitHub Actions still uses vcpkg on `windows-2022`; CI packaging is a
  follow-up, not part of the local Windows distribution path.

## Windows Distribution Command

Default local package:

```text
powershell -ExecutionPolicy Bypass -File scripts/package_windows.ps1 -Local
```

Package into a test dist directory:

```text
powershell -ExecutionPolicy Bypass -File scripts/package_windows.ps1 -Local -DistDir dist-package-test
```

Supported overrides:

```text
-BuildDir build-windows-release
-DistDir dist
-Msys2Root C:\msys64
-Sdl3Dll C:\path\to\SDL3.dll
-Generator Ninja
```

The script does not install MSYS2 packages. It validates that required MINGW64
tools and SDL3 are available and fails clearly if they are missing.

## Target Artifact Layout

The Windows package produces:

```text
dist/Vaporplane-windows-x64.zip
```

Inside:

```text
Vaporplane-windows-x64/
  vaporplane.exe
  SDL3.dll
  README_FIRST.txt
  THIRD_PARTY_NOTICES.md
  licenses/
    LGPL-2.1.txt
    CC0_License_For_Users.pdf
  resources/
    wav/
      [demo] Makaih Beats - Vibration.wav
      [demo] Makaih Beats - Vibration.wav.json
    drum_packs/
      00_vhs_cc0.json
      01_trap_808_cc0.json
      ...
      vhs-drumkit CC0/
      Trap 808 SignatureSounds.Org/
      ...
```

The app discovers packaged resources relative to `SDL_GetBasePath()`:

```text
<base>/../Resources
<base>/resources
```

The first path supports macOS app bundles. The second path supports the Windows
zip layout where `resources` sits beside `vaporplane.exe`.

Development fallback behavior remains:

```text
assets/samples
assets/drum_packs
exports/
```

## Script Behavior

The Windows package script runs these steps:

```text
validate MSYS2/MINGW64 tools
configure release build
build executable
run tests
create package directory
copy executable
locate/copy SDL3.dll
copy starter sample
copy starter drum packs
write tester quickstart
copy legal notices
verify package
zip
```

Defaults:

- MSYS2 root: `C:\msys64`
- MSYS environment: `MINGW64`
- Build directory: `build-windows-release`
- Dist directory: `dist`
- Package directory: `Vaporplane-windows-x64`
- Build type/config: `Release`
- CMake generator: `Ninja`

`SDL3.dll` lookup order:

1. Explicit `-Sdl3Dll`.
2. `$env:VAPORPLANE_SDL3_DLL`.
3. `C:\msys64\mingw64\bin\SDL3.dll` or the equivalent under `-Msys2Root`.
4. Fail with a clear message.

Do not silently ship without `SDL3.dll`.

## User Data Behavior To Verify

Packaged Windows builds should use SDL preference paths:

```text
samples/
drum_packs/
exports/roster/
exports/projects/
exports/renders/
```

Expected Windows preference area:

```text
%APPDATA%\Vaporplane\Vaporplane\
```

The exact resolved path should be confirmed from SDL on a real Windows run. The
sample selector shows the active WAV folder path.

First-launch seeding should copy:

- `[demo] Makaih Beats - Vibration.wav`
- `[demo] Makaih Beats - Vibration.wav.json`
- CC0 starter drum kit files/folders

Current sentinels:

```text
.makaih_vibration_demo_seeded
.cc0_starter_drum_packs_seeded
```

Deleting seeded content after first launch should not make it reappear unless
the matching sentinel is also removed.

## Legal And Notices Payload

The Windows zip must include:

- `THIRD_PARTY_NOTICES.md`
- `licenses/LGPL-2.1.txt`
- `licenses/CC0_License_For_Users.pdf`
- bundled demo sample JSON with `CC BY-NC-SA 4.0` metadata

Important license facts to preserve in README/notice text:

- SDL3 uses the zlib license.
- SoundTouch is LGPL v2.1 in this project.
- Bundled drum kits are documented as CC0 material.
- The Makaih Beats demo sample is CC BY-NC-SA 4.0 and is not for commercial use.

## Test Plan

Package smoke on Windows:

```text
powershell -ExecutionPolicy Bypass -File scripts/package_windows.ps1 -Local -DistDir dist-package-test
```

Artifact checks:

- `vaporplane.exe` exists.
- `SDL3.dll` exists beside `vaporplane.exe`.
- `README_FIRST.txt` exists.
- `THIRD_PARTY_NOTICES.md` exists.
- `licenses/LGPL-2.1.txt` exists.
- `licenses/CC0_License_For_Users.pdf` exists.
- `resources/wav/` contains only the Makaih demo WAV pair.
- `resources/drum_packs/` contains the eight curated kit JSONs.
- No `.DS_Store`, `._*`, or `.gitkeep` files are packaged.

Runtime smoke on Windows:

- Launch from the unpacked zip folder.
- Confirm it starts on a clean machine without installing SDL3 separately.
- Confirm user data folders are created under SDL's preference path.
- Confirm starter sample is copied once into user `samples/`.
- Confirm CC0 starter drum kits are copied once into user `drum_packs/`.
- Confirm a new drum lane defaults to `VHS Drumkit CC0`.
- Save a project and confirm it lands under user `exports/projects/`.

## CI Follow-Up

Leave GitHub Actions unchanged until the local MinGW package script is validated
on Windows.

Future CI work:

- Run `ctest --test-dir build --output-on-failure`.
- Add an optional Windows packaging job or step.
- Decide whether to switch Windows CI to MSYS2/MINGW64 or keep vcpkg as a CI-only
  build signal.
- Upload `Vaporplane-windows-x64.zip` as an artifact once CI packaging is
  intentionally enabled.

Keep packaging explicit. Do not make CI packaging imply that default local
development builds must package.
