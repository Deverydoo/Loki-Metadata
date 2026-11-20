# Builds a release: compiles, runs the tests, lays the app out fresh, then
# makes the portable zip and the installer in dist/.
#
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1
#
# The build folder must already be configured (see README). The version comes
# from CMakeLists.txt.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$version = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(LokiMetadata VERSION ([0-9.]+)').Groups[1].Value
if (-not $version) { throw "Can't find the version in CMakeLists.txt" }
Write-Host "Loki Metadata $version"

cmake --build build --config Release -- -m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "The build failed" }

Push-Location build
ctest -C Release --output-on-failure
$tests = $LASTEXITCODE
Pop-Location
if ($tests -ne 0) { throw "The tests failed; no release made" }

cmake --build build --config Release --target release_stage -- /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Laying out the release failed" }
$stage = Join-Path $root "build\release\Loki Metadata"

New-Item -ItemType Directory -Force dist | Out-Null
$zip = Join-Path $root "dist\LokiMetadata-$version-win64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

$iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
& $iscc /Q "/DMyAppVersion=$version" "/DSourceDir=$stage" installer\LokiMetadata.iss
if ($LASTEXITCODE -ne 0) { throw "The installer failed to build" }

Get-ChildItem dist | Where-Object { $_.Name -like "*$version*" } | ForEach-Object {
    $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
    "{0}  {1:N1} MB  sha256 {2}" -f $_.Name, ($_.Length / 1MB), $hash
}
