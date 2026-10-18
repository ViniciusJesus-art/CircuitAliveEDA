# Data files only: no binaries are imported from an installed KiCad/MSYS2 package.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$libraryRoot = Join-Path $root '.circuitalive/libraries'
$destination = Join-Path $libraryRoot 'share/kicad'
New-Item -ItemType Directory -Force $destination | Out-Null
$repositories = @{
    symbols = 'kicad-symbols'
    footprints = 'kicad-footprints'
    '3dmodels' = 'kicad-packages3D'
    template = 'kicad-templates'
}
foreach ($folder in $repositories.Keys) {
    $checkout = Join-Path $destination $folder
    if (!(Test-Path $checkout)) {
        & git clone --depth 1 --branch '10.0.7' `
            "https://gitlab.com/kicad/libraries/$($repositories[$folder]).git" $checkout
        if ($LASTEXITCODE -ne 0) { throw "Cannot prepare $folder" }
    }
    # Keep license/provenance while excluding Git metadata from the eventual package.
    & git -C $checkout rev-parse HEAD | Out-File "$libraryRoot/$folder-commit.txt"
    if ($LASTEXITCODE -ne 0) { throw "Cannot identify $folder" }
}
