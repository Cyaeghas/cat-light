$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (!(Test-Path $vswhere)) {
  throw "vswhere.exe not found. Install Visual Studio 2022 with Desktop development with C++."
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) {
  throw "Visual Studio C++ tools not found."
}

$cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (!(Test-Path $cmake)) {
  $cmake = "cmake"
}

Push-Location $root
try {
  & $cmake -S . -B build\msvc-release -G "Visual Studio 17 2022" -A x64
  & $cmake --build build\msvc-release --config Release
  Write-Host "Built: build\msvc-release\Release\cat-light.exe"
} finally {
  Pop-Location
}
