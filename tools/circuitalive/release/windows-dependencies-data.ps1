# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
function Get-WindowsDependencyProfile {
    param([string]$Root)
    $files = @('vcpkg.json', 'vcpkg-configuration.json',
        'tools/circuitalive/release/windows-toolchain.json',
        'tools/circuitalive/release/triplets/x64-windows.cmake',
        'tools/circuitalive/release/limit-windows-toolchain.ps1',
        'tools/circuitalive/release/build-windows.ps1',
        'tools/circuitalive/release/prepare-windows-dependencies.ps1',
        'tools/circuitalive/release/windows-dependencies-data.ps1')
    $hash = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
    try {
        foreach ($file in $files) {
            $hash.AppendData([Text.Encoding]::UTF8.GetBytes($file + "`n"))
            # Git's CRLF conversion must not change this profile between platforms.
            $hash.AppendData([Text.Encoding]::UTF8.GetBytes([IO.File]::ReadAllText((Join-Path $Root $file)).Replace("`r`n", "`n")))
        }
        [Convert]::ToHexString($hash.GetHashAndReset()).ToLowerInvariant()
    } finally { $hash.Dispose() }
}

function Write-WindowsDependencyManifest {
    param([string]$Root, [ValidateSet('foundation', 'simulation', 'cad', 'complete')][string]$Stage,
        [string]$Destination)
    $manifest = Get-Content "$Root/vcpkg.json" -Raw | ConvertFrom-Json -AsHashtable
    $excluded = switch ($Stage) {
        foundation { @('ngspice', 'opencascade', 'wxwidgets-33', 'wxpython-33') }
        simulation { @('opencascade', 'wxwidgets-33', 'wxpython-33') }
        cad { @('wxwidgets-33', 'wxpython-33') }
        complete { @() }
    }
    $manifest.dependencies = @($manifest.dependencies | Where-Object {
        $name = if ($_ -is [string]) { $_ } else { $_.name }
        $name -notin $excluded
    })
    New-Item -ItemType Directory -Force $Destination | Out-Null
    $manifest | ConvertTo-Json -Depth 30 | Set-Content "$Destination/vcpkg.json"
    Copy-Item "$Root/vcpkg-configuration.json" "$Destination/vcpkg-configuration.json"
    return (Resolve-Path $Destination).Path
}

function Get-WindowsDependencyArguments {
    param([string]$Root, [string]$ManifestDirectory, [switch]$Plan)
    $arguments = @('install', '--triplet=x64-windows', '--host-triplet=x64-windows',
        "--overlay-triplets=$Root/tools/circuitalive/release/triplets",
        "--x-manifest-root=$ManifestDirectory", "--x-install-root=$Root/build/windows-msvc/vcpkg_installed")
    if ($Plan) { $arguments += '--dry-run' }
    else { $arguments += @('--clean-buildtrees-after-build', '--clean-packages-after-build') }
    return $arguments
}

function Assert-WindowsDependenciesComplete {
    param([string]$Root)
    $stamp = Join-Path $Root 'build/windows-msvc/dependencies-complete.json'
    if (!(Test-Path $stamp)) { throw 'Prepare all Windows dependencies before CMake configuration (-Phase dependencies).' }
    $proof = Get-Content $stamp -Raw | ConvertFrom-Json
    if ($proof.profile -ne (Get-WindowsDependencyProfile $Root)) {
        throw 'The dependency profile changed. Prepare dependencies again before configuring CMake.'
    }
    if (!(Test-Path "$Root/build/windows-msvc/vcpkg_installed/vcpkg/status")) { throw 'The completed dependency installation is missing.' }
}
