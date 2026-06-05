# Windows Build And Distribution Baton Pass

## Summary

This document is the handoff for turning Vaporplane's current Windows developer
build into an explicit Windows distribution path. The macOS packaging work
should be treated as the model: normal development builds stay fast and plain,
while distribution work happens through a separate script/target.

Do not make Windows packaging part of the default build. Ordinary:

```text
cmake --build build
```

should continue to build only normal executable/test targets.

## Current State

- The app builds with CMake, C11/C++17, SDL3, and bundled SoundTouch.
- `CMakeLists.txt` links `vaporplane` against `SDL3::SDL3` and `SoundTouch`.
- SoundTouch is built from `third_party/soundtouch` with `SOUNDTOUCH_DLL OFF`,
  so the current project shape expects SoundTouch to be statically linked into
  the executable.
- A local Windows helper exists at `scripts/build-win-mingw.cmd`.
  - It assumes MSYS2 at `C:\msys64`.
  - It configures `build-mingw` with Ninja.
  - It copies `/mingw64/bin/SDL3.dll` into the build directory.
- GitHub Actions already builds Windows with vcpkg on `windows-2022`.
  - It installs `sdl3:x64-windows`.
  - It configures CMake with the vcpkg toolchain.
  - It builds Release, but currently does not run tests or create an artifact.
- macOS distribution currently packages:
  - executable
  - SDL3 runtime library
  - starter sample pair
  - starter drum packs
  - `THIRD_PARTY_NOTICES.md`
  - `LGPL-2.1.txt`
  - `CC0_License_For_Users.pdf`
  - tester `README_FIRST.txt`

## Recommended Windows Distribution Shape

Add a separate script, probably:

```text
scripts/package_windows.ps1
```

Recommended explicit modes:

```text
powershell -ExecutionPolicy Bypass -File scripts/package_windows.ps1 -Local
powershell -ExecutionPolicy Bypass -File scripts/package_windows.ps1 -DistDir dist-package-test
```

The first version does not need code signing. If signing is added later, keep it
explicit and credential-free by default:

```text
powershell -ExecutionPolicy Bypass -File scripts/package_windows.ps1 -Sign `
  -CertificatePath path\to\cert.pfx
```

Do not hardcode certificate paths, passwords, usernames, or machine-specific
tool locations. Use script arguments and environment variables only.

## Target Artifact Layout

The Windows package should produce a zip like:

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

The app currently discovers bundled resources relative to `SDL_GetBasePath()`.
For macOS this resolves through `Contents/MacOS/../Resources`. A Windows zip
should add an explicit sibling-resource fallback in `src/app.c` so the app can
find resources beside `vaporplane.exe`. Check both:

```text
<base>/../Resources
<base>/resources
```

Keep the fallback dev behavior intact:

```text
assets/samples
assets/drum_packs
exports/
```

## Script Requirements

The Windows packaging script should print each major step before doing it and
stop immediately on failure, including the failed command/status.

Suggested steps:

```text
configure release build
build executable
create package directory
copy executable
locate/copy SDL3.dll
copy starter sample
copy starter drum packs
write tester quickstart
copy legal notices
verify runtime dependencies
zip
```

Recommended defaults:

- Build directory: `build-windows-release`
- Dist directory: `dist`
- Package directory: `Vaporplane-windows-x64`
- Build type/config: `Release`
- CMake generator: let CMake choose by default unless a generator is supplied.
- Primary dependency path: vcpkg/MSVC, because CI already uses it.
- Optional local path: MSYS2/MinGW, because `scripts/build-win-mingw.cmd`
  already exists and copies `SDL3.dll`.

The script should accept overrides:

```text
-BuildDir build-windows-release
-DistDir dist
-Sdl3Dll C:\path\to\SDL3.dll
-CMakeToolchainFile C:\path\to\vcpkg.cmake
-VcpkgTriplet x64-windows
```

## SDL3.dll Handling

The package must include `SDL3.dll` beside `vaporplane.exe`.

Acceptable lookup order:

1. Explicit `-Sdl3Dll`.
2. `$env:VAPORPLANE_SDL3_DLL`.
3. vcpkg installed tree, if `VCPKG_INSTALLATION_ROOT` and triplet are known.
4. MSYS2 fallback: `C:\msys64\mingw64\bin\SDL3.dll`.
5. Fail with a clear message.

Do not silently ship without `SDL3.dll`.

## User Data Behavior To Verify

Packaged Windows builds should use SDL preference paths, matching macOS intent:

```text
samples/
drum_packs/
exports/roster/
exports/projects/
exports/renders/
```

Expected Windows location is likely under:

```text
%APPDATA%\Vaporplane\Vaporplane\
```

Verify this on a real Windows run; do not assume the exact resolved path until
SDL reports it.

First-launch seeding should copy:

- `[demo] Makaih Beats - Vibration.wav`
- `[demo] Makaih Beats - Vibration.wav.json`
- CC0 starter drum kit files/folders

Current sentinels:

```text
.makaih_vibration_demo_seeded
.cc0_starter_drum_packs_seeded
```

Deleting the seeded sample or drum kits after first launch should not make them
reappear unless the sentinel is also removed.

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

Local or CI build:

```text
cmake -S . -B build-windows-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows-release --config Release --parallel
ctest --test-dir build-windows-release --output-on-failure
```

Package smoke:

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
- No stale RealDrumSamples strings are present in the package.

Runtime smoke on Windows:

- Launch from the unpacked zip folder.
- Confirm it starts on a clean machine without installing SDL3 separately.
- Confirm user data folders are created under SDL's preference path.
- Confirm starter sample is copied once into user `samples/`.
- Confirm CC0 starter drum kits are copied once into user `drum_packs/`.
- Confirm a new drum lane defaults to `VHS Drumkit CC0`.
- Save a project and confirm it lands under user `exports/projects/`.

## CI Follow-Up

After the script works locally, extend `.github/workflows/build.yml`:

- Run `ctest --test-dir build --output-on-failure`.
- Add an optional packaging job or packaging step for Windows.
- Upload `Vaporplane-windows-x64.zip` as an artifact.

Keep packaging explicit. Do not make CI packaging imply that local default
development builds must package.

## Open Questions For The Implementer

- Should the first Windows package target MSVC/vcpkg only, or also support
  MSYS2/MinGW as a first-class package mode?
- Is unsigned zip distribution acceptable for early testers, or should a later
  explicit signing mode be planned before wider distribution?
