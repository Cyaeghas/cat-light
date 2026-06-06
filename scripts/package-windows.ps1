param(
  [string]$BuildDir = "build\msvc-release",
  [string]$Configuration = "Release",
  [string]$OutDir = "dist\packages",
  [string]$Version = "dev"
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildPath = Join-Path $root $BuildDir
$releasePath = Join-Path $buildPath $Configuration
$packageName = "cat-light-windows-msvc-$Version"
$stage = Join-Path $OutDir $packageName
$zip = Join-Path $OutDir "$packageName.zip"

$required = @(
  "cat-light.exe",
  "cat-light-tray.exe",
  "cat-light-float.exe"
)

foreach ($file in $required) {
  $path = Join-Path $releasePath $file
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing build output: $path"
  }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (Test-Path -LiteralPath $stage) {
  Remove-Item -LiteralPath $stage -Recurse -Force
}
if (Test-Path -LiteralPath $zip) {
  Remove-Item -LiteralPath $zip -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "docs") | Out-Null

foreach ($file in $required) {
  Copy-Item -LiteralPath (Join-Path $releasePath $file) -Destination $stage
}

Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "README.zh-CN.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "docs\install-windows.md") -Destination (Join-Path $stage "docs")

Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -Force

Write-Host "Created $zip"
