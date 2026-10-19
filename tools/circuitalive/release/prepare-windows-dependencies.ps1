# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [ValidateSet('all', 'gperf', 'foundation', 'simulation', 'cad', 'complete')][string]$Stage = 'all',
    [switch]$Plan,
    [ValidateRange(1, 2)][int]$Jobs = 2,
    [string]$ToolsRoot = $env:CA_WINDOWS_TOOLS,
    [string]$OutputDirectory = 'build/windows-dependencies'
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'Dependency preparation requires native Windows/MSVC.' }
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
Set-Location $root
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
. "$PSScriptRoot/windows-dependencies-data.ps1"
. "$PSScriptRoot/windows-audit-data.ps1"
. "$PSScriptRoot/windows-monitor-control.ps1"
& "$PSScriptRoot/build-windows.ps1" -Phase environment -ToolsRoot $ToolsRoot -Jobs $Jobs
$vcpkg = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
if (!(Test-Path $vcpkg)) { throw 'Run tool setup before dependency preparation.' }
. "$PSScriptRoot/limit-windows-toolchain.ps1"
$lock = Get-Content "$PSScriptRoot/windows-toolchain.json" -Raw | ConvertFrom-Json
Set-LegacyMsbuildLimit -Path "$env:VCPKG_ROOT/scripts/cmake/vcpkg_build_msbuild.cmake" -OriginalSha256 $lock.legacyMsbuildHelperSha256
$profile = Get-WindowsDependencyProfile $root
$stages = if ($Stage -eq 'all') { @('foundation', 'simulation', 'cad', 'complete') } else { @($Stage) }
if ($Stage -eq 'all' -and !$Plan -and (Test-Path "$root/build/windows-msvc/dependencies-complete.json")) {
    # Reuse a complete installation instead of reducing it to the first stage.
    $complete = $false
    try { Assert-WindowsDependenciesComplete $root; $complete = $true } catch { }
    if ($complete) { Write-Host 'The full dependency installation already matches this profile.'; return }
}
foreach ($current in $stages) {
    Set-Content "$output/current-phase.txt" "dependencies-$current"
    $manifest = Write-WindowsDependencyManifest -Root $root -Stage $current -Destination "$output/manifests/$current"
    $arguments = @(Get-WindowsDependencyArguments -Root $root -ManifestDirectory $manifest -Plan:$Plan)
    $label = if ($Plan) { "$current-plan" } else { "$current-install" }
    [ordered]@{
        timestampUtc = [DateTime]::UtcNow.ToString('o'); stage = $current; profile = $profile
        command = @($vcpkg) + $arguments; dependencyBuildsAllowed = !$Plan; applicationBuildsAllowed = $false
        buildType = 'release'; vcpkgConcurrency = $env:VCPKG_MAX_CONCURRENCY
        cmakeParallelLevel = $env:CMAKE_BUILD_PARALLEL_LEVEL; msvcFlags = $env:_CL_
        msysEnvironmentConversionExclusions = $env:MSYS2_ENV_CONV_EXCL
        compiler = $env:VCToolsVersion; sdk = $env:WindowsSDKVersion; binarySources = $env:VCPKG_BINARY_SOURCES
    } | ConvertTo-Json -Depth 5 | Set-Content "$output/$label-command.json"
    # A failed/restarted preparation cannot leave an old completion proof behind.
    if (!$Plan) { Remove-Item "$root/build/windows-msvc/dependencies-complete.json" -ErrorAction SilentlyContinue }
    $started = Get-Date
    Start-ResourceMonitor
    try {
        & $vcpkg @arguments 2>&1 | Tee-Object -FilePath "$output/$label.log"
        $exitCode = $LASTEXITCODE
        $result = Get-VcpkgAuditResult -Text (Get-Content "$output/$label.log" -Raw) -ExitCode $exitCode
        $result.outcome = if ($exitCode -eq 0) { 'complete' } else { 'error' }
        $result | Add-Member NoteProperty elapsedSeconds ((Get-Date) - $started).TotalSeconds
        $result | ConvertTo-Json -Depth 6 | Set-Content "$output/$label-result.json"
        Get-VcpkgCacheInventory $env:VCPKG_DEFAULT_BINARY_CACHE | ConvertTo-Json | Set-Content "$output/$label-cache.json"
        if ($exitCode -ne 0) { throw "Dependency stage $current failed ($exitCode); see $label.log." }
        if ($current -eq 'complete' -and !$Plan) {
            @{ profile = $profile; completedUtc = [DateTime]::UtcNow.ToString('o') } |
                ConvertTo-Json | Set-Content "$root/build/windows-msvc/dependencies-complete.json"
        }
        $global:LASTEXITCODE = 0
    } finally { Stop-ResourceMonitor }
}
