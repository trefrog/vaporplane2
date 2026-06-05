@echo off
setlocal

for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "MSYS=C:\msys64\usr\bin\bash.exe"

if not exist "%MSYS%" (
  echo ERROR: MSYS2 bash not found at %MSYS%
  pause
  exit /b 1
)

"%MSYS%" --login -i -c "cd ""$(cygpath -u '%REPO%')"" && cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-mingw && cp /mingw64/bin/SDL3.dll ./build-mingw/"

echo.
echo Build finished.
pause
