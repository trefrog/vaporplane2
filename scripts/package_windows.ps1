param(
    [switch]$Local,
    [switch]$Help,
    [string]$BuildDir = "build-windows-release",
    [string]$DistDir = "dist",
    [string]$Msys2Root = "C:\msys64",
    [string]$Sdl3Dll = "",
    [string]$Generator = "Ninja"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$PackageName = "Vaporplane-windows-x64"
$BuildType = "Release"
$MsysEnvironment = "MINGW64"
$StarterSample = "[demo] Makaih Beats - Vibration.wav"
$StarterSampleJson = "[demo] Makaih Beats - Vibration.wav.json"
$CuratedDrumPackJsons = @(
    "00_vhs_cc0.json",
    "01_trap_808_cc0.json",
    "02_fred_again_style_cc0.json",
    "03_burial_kicks_cc0.json",
    "04_kick_drums_multi_genre_cc0.json",
    "05_cowbells_cc0.json",
    "06_cymbal_crashes_cc0.json",
    "07_stick_snaps_cc0.json"
)
$MingwRuntimeDlls = @(
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll"
)

function Show-Usage {
    Write-Host @"
Usage:
  powershell -ExecutionPolicy Bypass -File scripts/package_windows.ps1 -Local [options]

Options:
  -BuildDir DIR      Release build directory (default: build-windows-release)
  -DistDir DIR       Distribution output directory (default: dist)
  -Msys2Root DIR     MSYS2 installation root (default: C:\msys64)
  -Sdl3Dll PATH      SDL3.dll to bundle instead of auto-detecting
  -Generator NAME    CMake generator (default: Ninja)
  -Help              Show this help

Environment alternatives:
  VAPORPLANE_SDL3_DLL
"@
}

if ($Help) {
    Show-Usage
    exit 0
}

if (-not $Local) {
    throw "Choose -Local. Windows signing is not implemented for this package script."
}

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Bash = Join-Path $Msys2Root "usr\bin\bash.exe"

function Resolve-RepoPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $RootDir $Path))
}

$BuildDirAbs = Resolve-RepoPath $BuildDir
$DistDirAbs = Resolve-RepoPath $DistDir
$PackageDir = Join-Path $DistDirAbs $PackageName
$ZipPath = Join-Path $DistDirAbs "$PackageName.zip"

if ((Split-Path -Leaf $PackageDir) -ne $PackageName) {
    throw "Refusing to package into unexpected output path: $PackageDir"
}

function Step([string]$Text) {
    Write-Host ""
    Write-Host "==> $Text"
}

function Quote-BashSingle([string]$Value) {
    return "'" + $Value.Replace("'", "'\''") + "'"
}

function Get-NativeExitCode([bool]$Success) {
    $lastExit = Get-Variable -Name LASTEXITCODE -Scope Global -ErrorAction SilentlyContinue
    if ($null -ne $lastExit) {
        return [int]$lastExit.Value
    }
    if ($Success) {
        return 0
    }
    return 1
}

function Convert-ToMsysPath([string]$Path) {
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($fullPath -match "^([A-Za-z]):\\(.*)$") {
        $drive = $Matches[1].ToLowerInvariant()
        $rest = $Matches[2].Replace("\", "/")
        if ($rest) {
            return "/$drive/$rest"
        }
        return "/$drive"
    }
    return $fullPath.Replace("\", "/")
}

function Invoke-Msys([string]$Description, [string]$Command) {
    Step $Description
    Write-Host "+ MSYSTEM=$MsysEnvironment bash --login -lc $Command"

    $stdoutPath = Join-Path ([System.IO.Path]::GetTempPath()) ("vaporplane-msys-stdout-" + [System.Guid]::NewGuid().ToString("N") + ".log")
    $stderrPath = Join-Path ([System.IO.Path]::GetTempPath()) ("vaporplane-msys-stderr-" + [System.Guid]::NewGuid().ToString("N") + ".log")
    $oldMsystem = $env:MSYSTEM
    $oldChereInvoking = $env:CHERE_INVOKING
    $oldMsys2PathType = $env:MSYS2_PATH_TYPE

    $env:MSYSTEM = $MsysEnvironment
    $env:CHERE_INVOKING = "1"
    $env:MSYS2_PATH_TYPE = "inherit"
    try {
        & $Bash --login -lc $Command > $stdoutPath 2> $stderrPath
        $exitCode = Get-NativeExitCode $?
        if (Test-Path -LiteralPath $stdoutPath) {
            Get-Content -LiteralPath $stdoutPath | ForEach-Object { Write-Host $_ }
        }
        if (Test-Path -LiteralPath $stderrPath) {
            Get-Content -LiteralPath $stderrPath | ForEach-Object { Write-Host $_ }
        }
        if ($exitCode -ne 0) {
            throw "FAILED: $Description (status $exitCode)"
        }
    } finally {
        if (Test-Path -LiteralPath $stdoutPath) {
            Remove-Item -LiteralPath $stdoutPath -Force
        }
        if (Test-Path -LiteralPath $stderrPath) {
            Remove-Item -LiteralPath $stderrPath -Force
        }
        $env:MSYSTEM = $oldMsystem
        $env:CHERE_INVOKING = $oldChereInvoking
        $env:MSYS2_PATH_TYPE = $oldMsys2PathType
    }
}

function Copy-FilteredDirectory([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        throw "Source directory not found: $Source"
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null

    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        if ($_.Name -eq ".DS_Store" -or $_.Name -eq ".gitkeep" -or $_.Name.StartsWith("._")) {
            return
        }

        $target = Join-Path $Destination $_.Name
        if ($_.PSIsContainer) {
            Copy-FilteredDirectory $_.FullName $target
        } else {
            Copy-Item -LiteralPath $_.FullName -Destination $target -Force
        }
    }
}

function Resolve-Sdl3Dll {
    if ($Sdl3Dll) {
        if (Test-Path -LiteralPath $Sdl3Dll -PathType Leaf) {
            return (Resolve-Path -LiteralPath $Sdl3Dll).Path
        }
        throw "SDL3.dll not found from -Sdl3Dll: $Sdl3Dll"
    }

    if ($env:VAPORPLANE_SDL3_DLL) {
        if (Test-Path -LiteralPath $env:VAPORPLANE_SDL3_DLL -PathType Leaf) {
            return (Resolve-Path -LiteralPath $env:VAPORPLANE_SDL3_DLL).Path
        }
        throw "SDL3.dll not found from VAPORPLANE_SDL3_DLL: $env:VAPORPLANE_SDL3_DLL"
    }

    $msysSdl3 = Join-Path $Msys2Root "mingw64\bin\SDL3.dll"
    if (Test-Path -LiteralPath $msysSdl3 -PathType Leaf) {
        return (Resolve-Path -LiteralPath $msysSdl3).Path
    }

    throw "Could not locate SDL3.dll. Pass -Sdl3Dll, set VAPORPLANE_SDL3_DLL, or install mingw-w64-x86_64-SDL3 under $Msys2Root."
}

function Copy-MingwRuntimeDlls {
    $mingwBin = Join-Path $Msys2Root "mingw64\bin"
    foreach ($dll in $MingwRuntimeDlls) {
        $source = Join-Path $mingwBin $dll
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "MinGW runtime DLL not found: $source"
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $PackageDir $dll) -Force
    }
}

function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file missing: $Path"
    }
}

function Get-BuiltExecutablePath {
    $exeCandidates = @(
        (Join-Path $BuildDirAbs "vaporplane.exe"),
        (Join-Path $BuildDirAbs "Release\vaporplane.exe")
    )
    return $exeCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}

function Verify-Package {
    Step "verify package"

    Require-File (Join-Path $PackageDir "vaporplane.exe")
    Require-File (Join-Path $PackageDir "SDL3.dll")
    foreach ($dll in $MingwRuntimeDlls) {
        Require-File (Join-Path $PackageDir $dll)
    }
    Require-File (Join-Path $PackageDir "README_FIRST.txt")
    Require-File (Join-Path $PackageDir "THIRD_PARTY_NOTICES.md")
    Require-File (Join-Path $PackageDir "licenses\LGPL-2.1.txt")
    Require-File (Join-Path $PackageDir "licenses\CC0_License_For_Users.pdf")
    Require-File (Join-Path $PackageDir "resources\wav\$StarterSample")
    Require-File (Join-Path $PackageDir "resources\wav\$StarterSampleJson")

    $wavDir = Join-Path $PackageDir "resources\wav"
    $wavNames = @(Get-ChildItem -LiteralPath $wavDir -File | ForEach-Object { $_.Name } | Sort-Object)
    $expectedWavs = @($StarterSample, $StarterSampleJson) | Sort-Object
    if (($wavNames -join "`n") -ne ($expectedWavs -join "`n")) {
        throw "resources\wav must contain only the Makaih demo WAV pair."
    }

    $drumPackDir = Join-Path $PackageDir "resources\drum_packs"
    foreach ($json in $CuratedDrumPackJsons) {
        Require-File (Join-Path $drumPackDir $json)
    }

    $excluded = @(Get-ChildItem -LiteralPath $PackageDir -Recurse -Force | Where-Object {
        $_.Name -eq ".DS_Store" -or $_.Name -eq ".gitkeep" -or $_.Name.StartsWith("._")
    })
    if ($excluded.Count -gt 0) {
        $names = ($excluded | ForEach-Object { $_.FullName }) -join "`n"
        throw "Excluded metadata files found in package:`n$names"
    }
}

if (-not (Test-Path -LiteralPath $Bash -PathType Leaf)) {
    throw "MSYS2 bash not found: $Bash"
}

$soundTouchCmake = Join-Path $RootDir "third_party\soundtouch\CMakeLists.txt"
$soundTouchCopying = Join-Path $RootDir "third_party\soundtouch\COPYING.TXT"
if (-not (Test-Path -LiteralPath $soundTouchCmake -PathType Leaf) -or
    -not (Test-Path -LiteralPath $soundTouchCopying -PathType Leaf)) {
    throw "SoundTouch submodule is missing or incomplete. Run: git submodule update --init --recursive"
}

$RootMsys = Convert-ToMsysPath $RootDir
$BuildMsys = Convert-ToMsysPath $BuildDirAbs

$toolCheck = @"
command -v cmake >/dev/null || { echo 'cmake not found in MSYS2 MINGW64 PATH' >&2; exit 127; }
(command -v cc >/dev/null || command -v gcc >/dev/null) || { echo 'C compiler not found in MSYS2 MINGW64 PATH' >&2; exit 127; }
command -v c++ >/dev/null || command -v g++ >/dev/null || { echo 'C++ compiler not found in MSYS2 MINGW64 PATH' >&2; exit 127; }
test -f /mingw64/lib/cmake/SDL3/SDL3Config.cmake || { echo 'SDL3 CMake package not found at /mingw64/lib/cmake/SDL3/SDL3Config.cmake' >&2; exit 1; }
"@

if ($Generator -like "*Ninja*") {
    $toolCheck += "`ncommand -v ninja >/dev/null || { echo 'ninja not found in MSYS2 MINGW64 PATH' >&2; exit 127; }"
}

Invoke-Msys "validate MSYS2/MINGW64 tools" $toolCheck
Invoke-Msys "configure release build" "cd $(Quote-BashSingle $RootMsys) && cmake -S . -B $(Quote-BashSingle $BuildMsys) -G $(Quote-BashSingle $Generator) -DCMAKE_BUILD_TYPE=$BuildType"
if ($Generator -like "*Ninja*") {
    Require-File (Join-Path $BuildDirAbs "build.ninja")
}
Invoke-Msys "build executable" "cd $(Quote-BashSingle $RootMsys) && cmake --build $(Quote-BashSingle $BuildMsys) --target vaporplane --parallel"
$builtExecutable = Get-BuiltExecutablePath
if (-not $builtExecutable) {
    throw "Build completed without producing vaporplane.exe in: $BuildDirAbs"
}
Invoke-Msys "run tests" "cd $(Quote-BashSingle $RootMsys) && ctest --test-dir $(Quote-BashSingle $BuildMsys) --output-on-failure"

Step "create package directory"
New-Item -ItemType Directory -Force -Path $DistDirAbs | Out-Null
if (Test-Path -LiteralPath $PackageDir) {
    Remove-Item -LiteralPath $PackageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageDir "licenses") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageDir "resources\wav") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageDir "resources\drum_packs") | Out-Null

Step "copy executable"
Copy-Item -LiteralPath $builtExecutable -Destination (Join-Path $PackageDir "vaporplane.exe") -Force

Step "locate/copy SDL3.dll"
$resolvedSdl3Dll = Resolve-Sdl3Dll
Copy-Item -LiteralPath $resolvedSdl3Dll -Destination (Join-Path $PackageDir "SDL3.dll") -Force

Step "copy MinGW runtime DLLs"
Copy-MingwRuntimeDlls

Step "copy starter sample"
$wavDir = Join-Path $PackageDir "resources\wav"
Copy-Item -LiteralPath (Join-Path $RootDir "assets\samples\$StarterSample") -Destination (Join-Path $wavDir $StarterSample) -Force
Copy-Item -LiteralPath (Join-Path $RootDir "assets\samples\$StarterSampleJson") -Destination (Join-Path $wavDir $StarterSampleJson) -Force

Step "copy starter drum packs"
Copy-FilteredDirectory (Join-Path $RootDir "assets\drum_packs") (Join-Path $PackageDir "resources\drum_packs")

Step "write tester quickstart"
$readme = @"
Vaporplane Quickstart
=====================

First move:
- Press F1 in the app to show/hide the controls legend.
- Press F2 or R2+Start to cycle Waveform, Timeline, and Master Mix.
- The packaged starter sample is copied into your user samples folder on first launch.
- The packaged starter sample is CC BY-NC-SA 4.0 material and is not for commercial use.
- The packaged CC0 starter drum kits are copied into your user drum_packs folder on first launch.

User folders:
- Expected Windows preference area: %APPDATA%\Vaporplane\Vaporplane\
- WAVs: samples\
- Drum packs: drum_packs\
- Exports: exports\
- The sample selector shows the active WAV folder path at the top.

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
- New drum lanes default to the VHS Drumkit CC0 kit.
- In the drum machine, D-pad moves, South toggles a step, bumpers change velocity, and East returns to the timeline.
- Put custom drum kit JSON and WAV folders in the user drum_packs folder.
- Kits can be top-level JSON files or folders containing kit.json.

Projects:
- Escape from idle Timeline opens the Project menu.
- Saved .vapor projects go under exports\projects.
"@
$readme | Set-Content -LiteralPath (Join-Path $PackageDir "README_FIRST.txt") -Encoding ASCII

Step "copy legal notices"
Copy-Item -LiteralPath (Join-Path $RootDir "THIRD_PARTY_NOTICES.md") -Destination (Join-Path $PackageDir "THIRD_PARTY_NOTICES.md") -Force
Copy-Item -LiteralPath (Join-Path $RootDir "third_party\soundtouch\COPYING.TXT") -Destination (Join-Path $PackageDir "licenses\LGPL-2.1.txt") -Force
Copy-Item -LiteralPath (Join-Path $RootDir "CC0_License_For_Users.pdf") -Destination (Join-Path $PackageDir "licenses\CC0_License_For_Users.pdf") -Force

Verify-Package

Step "zip"
if (Test-Path -LiteralPath $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}
Compress-Archive -LiteralPath $PackageDir -DestinationPath $ZipPath -CompressionLevel Optimal

Write-Host ""
Write-Host "Packaged: $ZipPath"
