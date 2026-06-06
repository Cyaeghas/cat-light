$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$bash = "C:\msys64\usr\bin\bash.exe"

if (!(Test-Path $bash)) {
  throw "MSYS2 bash not found at C:\msys64\usr\bin\bash.exe"
}

$unixRoot = $root -replace "\\", "/"
$unixRoot = $unixRoot -replace "^([A-Za-z]):", '/$1'
$unixRoot = $unixRoot.ToLowerInvariant()

& $bash -lc "export PATH=/ucrt64/bin:`$PATH; cd '$unixRoot' && cmake -S . -B build/msys2-ucrt64-release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/msys2-ucrt64-release"
