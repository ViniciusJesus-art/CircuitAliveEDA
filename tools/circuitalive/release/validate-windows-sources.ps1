# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [switch]$Plan,
    [string]$CompileCommands = 'build/windows-msvc/compile_commands.json',
    [string]$OutputDirectory = 'build/windows-validation',
    [ValidateRange(1, 2)][int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version Latest
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
Set-Location $root
$build = Join-Path $root 'build/windows-msvc'
$output = (New-Item -ItemType Directory -Force $OutputDirectory).FullName
$commands = Get-Content $CompileCommands -Raw | ConvertFrom-Json
$selected = @{}
$seen = @{}
foreach ($entry in $commands) {
    $source = [IO.Path]::GetRelativePath($root, $entry.file).Replace('\', '/')
    if ($source.StartsWith('../') -or !($source -match '\.cpp$')) { continue }
    if (!$seen.ContainsKey($source)) {
        $seen[$source] = $source.StartsWith('eeschema/circuitalive/') -or
            $source.StartsWith('common/circuitalive/') -or
            $source -eq 'eeschema/sim/platform_runtime.cpp' -or
            $source -eq 'kicad/pcm/dialogs/dialog_manage_repositories.cpp'
        if (!$seen[$source] -and (Test-Path -LiteralPath $entry.file)) {
            $seen[$source] = [bool](Select-String -LiteralPath $entry.file -SimpleMatch -Quiet -Pattern 'circuitalive/branding.h')
        }
    }
    if (!$seen[$source]) { continue }
    if ($entry.command -notmatch '(?:^|\s)/Fo(?:"(?<quoted>[^"]+)"|(?<plain>[^\s"]+))') {
        throw "Missing native MSVC object output for $source"
    }
    $target = ($Matches['quoted'] + $Matches['plain']).Replace('\', '/')
    if ([IO.Path]::IsPathRooted($target) -or $target.Contains('../') -or !$target.EndsWith('.obj')) {
        throw "Expected a relative MSVC object target for ${source}: $target"
    }
    $selected[$target] = $source
}
if (!$selected.Count -or 'eeschema/circuitalive/model_author.cpp' -notin $selected.Values -or
    'kicad/pcm/dialogs/dialog_manage_repositories.cpp' -notin $selected.Values) {
    throw 'The source plan is incomplete; configure the full Windows preset first.'
}
$objects = @($selected.Keys | Sort-Object)
$arguments = @('--build', $build, '--parallel', "$Jobs", '--target', 'qa_interactive_sim') + $objects + @('--', '-k', '0')
[ordered]@{
    sourceCount = @($selected.Values | Sort-Object -Unique).Count
    objectCount = $objects.Count; fullApplicationBuild = $false
    command = @('cmake') + $arguments
    objects = @($objects | ForEach-Object { @{ target = $_; source = $selected[$_] } })
} | ConvertTo-Json -Depth 6 | Set-Content "$output/source-plan.json"
Write-Host "Portability validation: $($objects.Count) objects plus qa_interactive_sim; Ninja -k 0, jobs=$Jobs"
if ($Plan) { return }
& "$PSScriptRoot/build-windows.ps1" -Phase environment -Jobs $Jobs
# Keep compiling independent objects after failures, while preserving a failing exit status.
& cmake @arguments 2>&1 | Tee-Object -FilePath "$output/compile.log"
$exitCode = $LASTEXITCODE
$diagnostics = @(Get-Content "$output/compile.log" | Where-Object {
    $_ -match 'FAILED:|(?:fatal )?error (?:C|LNK)\d+:|ninja: (?:error|build stopped)'
})
@{ exitCode = $exitCode; diagnostics = $diagnostics } | ConvertTo-Json -Depth 4 |
    Set-Content "$output/compile-result.json"
if ($exitCode -ne 0) { throw "MSVC source validation failed ($exitCode); see compile-result.json and compile.log." }
# Reuse runtime preparation and CTest only after all selected objects compile.
& "$PSScriptRoot/build-windows.ps1" -Phase validate -Jobs $Jobs
