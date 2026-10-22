# Install component data using the same upstream rules as the Linux source packages.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
if (!(Get-Command ninja -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vs = & $vswhere -latest -products '*' -property installationPath
    $ninja = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
    if (!(Test-Path "$ninja/ninja.exe")) { throw 'Ninja is required to prepare component libraries' }
    $env:PATH = "$ninja;$env:PATH"
}
& python "$PSScriptRoot/prepare-source-libraries.py" --directory "$root/.circuitalive/libraries/share/kicad"
if ($LASTEXITCODE -ne 0) { throw 'Cannot build and install the bundled component libraries' }
