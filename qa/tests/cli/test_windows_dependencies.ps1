# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
param([string]$HelperPath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
. "$root/tools/circuitalive/release/windows-dependencies-data.ps1"
. "$root/tools/circuitalive/release/limit-windows-toolchain.ps1"
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('ca-dependencies-qa-' + [guid]::NewGuid())
New-Item -ItemType Directory $temporary | Out-Null
try {
    $previous = @()
    $original = Get-Content "$root/vcpkg.json" -Raw | ConvertFrom-Json -AsHashtable
    foreach ($stage in @('foundation', 'simulation', 'cad', 'complete')) {
        $directory = Write-WindowsDependencyManifest -Root $root -Stage $stage -Destination "$temporary/$stage"
        $manifest = Get-Content "$directory/vcpkg.json" -Raw | ConvertFrom-Json -AsHashtable
        $names = @($manifest.dependencies | ForEach-Object { if ($_ -is [string]) { $_ } else { $_.name } })
        if (@($previous | Where-Object { $_ -notin $names }).Count) { throw 'A later dependency stage removes an earlier root' }
        if (($manifest.overrides | ConvertTo-Json -Depth 20) -ne ($original.overrides | ConvertTo-Json -Depth 20)) { throw 'Pinned versions changed' }
        if ((Get-Content "$directory/vcpkg-configuration.json" -Raw) -ne (Get-Content "$root/vcpkg-configuration.json" -Raw)) { throw 'Registry configuration changed' }
        if ($stage -eq 'complete' -and ($manifest | ConvertTo-Json -Depth 30) -ne ($original | ConvertTo-Json -Depth 30)) { throw 'The complete manifest lost features/dependencies' }
        $arguments = @(Get-WindowsDependencyArguments -Root $root -ManifestDirectory $directory)
        if ($arguments[0] -ne 'install' -or '--clean-buildtrees-after-build' -notin $arguments -or
            '--clean-packages-after-build' -notin $arguments -or "--x-install-root=$root/build/windows-msvc/vcpkg_installed" -notin $arguments) { throw 'Invalid explicit installation arguments' }
        $previous = $names
    }
    Write-Output 'PASS: cumulative dependency manifests preserve all final features, pins, registries and installed path'

    # Patching is narrow, idempotent and refuses any unknown vendor helper.
    $fixture = "function(f)`n  set(args `"/m`")`nendfunction()`n"
    $sha = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($fixture))).ToLowerInvariant()
    $helper = "$temporary/fixture.cmake"
    [IO.File]::WriteAllText($helper, $fixture.Replace("`n", "`r`n"))
    Set-LegacyMsbuildLimit $helper $sha
    Set-LegacyMsbuildLimit $helper $sha
    if ((Get-Content $helper -Raw) -notmatch [regex]::Escape('"/m:${VCPKG_CONCURRENCY}"')) { throw 'Legacy /m is still unbounded' }
    Add-Content $helper 'unexpected change'
    $rejected = $false
    try { Set-LegacyMsbuildLimit $helper $sha } catch { $rejected = $true }
    if (!$rejected) { throw 'An unknown MSBuild helper was silently patched' }
    Write-Output 'PASS: legacy helper patch is bounded, repeatable and checked against pinned content'

    # A stale marker cannot silently bypass dependency installation.
    $profileRoot = "$temporary/profile"
    foreach ($relative in @('vcpkg.json','vcpkg-configuration.json',
        'tools/circuitalive/release/windows-toolchain.json','tools/circuitalive/release/triplets/x64-windows.cmake',
        'tools/circuitalive/release/limit-windows-toolchain.ps1','tools/circuitalive/release/windows-dependencies-data.ps1',
        'tools/circuitalive/release/build-windows.ps1','tools/circuitalive/release/prepare-windows-dependencies.ps1')) {
        $target = Join-Path $profileRoot $relative
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item (Join-Path $root $relative) $target
    }
    $profile = Get-WindowsDependencyProfile $profileRoot
    $rejected = $false
    try { Assert-WindowsDependenciesComplete $profileRoot } catch { $rejected = $true }
    if (!$rejected) { throw 'Missing dependency proof was accepted' }
    New-Item -ItemType Directory -Force "$profileRoot/build/windows-msvc/vcpkg_installed/vcpkg" | Out-Null
    Set-Content "$profileRoot/build/windows-msvc/vcpkg_installed/vcpkg/status" 'fixture'
    @{ profile = $profile } | ConvertTo-Json | Set-Content "$profileRoot/build/windows-msvc/dependencies-complete.json"
    Assert-WindowsDependenciesComplete $profileRoot
    Add-Content "$profileRoot/vcpkg.json" ' '
    $rejected = $false
    try { Assert-WindowsDependenciesComplete $profileRoot } catch { $rejected = $true }
    if (!$rejected) { throw 'A stale completion proof was accepted' }
    Write-Output 'PASS: missing/stale dependency preparation prevents CMake configuration'

    if ($HelperPath) {
        $lock = Get-Content "$root/tools/circuitalive/release/windows-toolchain.json" -Raw | ConvertFrom-Json
        $bounded = "$temporary/bounded.cmake"
        Copy-Item $HelperPath $bounded
        Set-LegacyMsbuildLimit $bounded $lock.legacyMsbuildHelperSha256
        $check = @'
cmake_minimum_required(VERSION 3.22)
set(VCPKG_CONCURRENCY 2)
set(VCPKG_PLATFORM_TOOLSET v143)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
set(CURRENT_BUILDTREES_DIR "${CMAKE_CURRENT_LIST_DIR}/buildtrees")
function(vcpkg_execute_required_process)
    if(NOT "/m:2" IN_LIST ARGV OR "/m" IN_LIST ARGV)
        message(FATAL_ERROR "Unbounded/wrong MSBuild parallelism: ${ARGV}")
    endif()
    if(NOT "/p:Configuration=Release" IN_LIST ARGV OR "/p:Configuration=Debug" IN_LIST ARGV)
        message(FATAL_ERROR "Unexpected dependency configuration: ${ARGV}")
    endif()
    file(WRITE "${CMAKE_CURRENT_LIST_DIR}/checked" "OK")
endfunction()
include("${CMAKE_CURRENT_LIST_DIR}/bounded.cmake")
vcpkg_build_msbuild(PROJECT_PATH unused.sln PLATFORM x64 TARGET_PLATFORM_VERSION 10.0)
'@
        Set-Content "$temporary/check.cmake" $check
        & cmake -P "$temporary/check.cmake"
        if ($LASTEXITCODE -ne 0 -or !(Test-Path "$temporary/checked")) { throw 'Actual legacy helper invocation failed' }
        Write-Output 'PASS: actual pinned helper invokes MSBuild with /m:2 and Release only (no compiler executed)'
    }
} finally { Remove-Item $temporary -Recurse -Force -ErrorAction SilentlyContinue }

Get-ChildItem "$root/tools/circuitalive/release" -Filter '*.ps1' | ForEach-Object {
    $tokens = $null; $errors = $null
    [void][Management.Automation.Language.Parser]::ParseFile($_.FullName, [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw ($errors | Out-String) }
}
Write-Output 'PASS: release script syntax'
