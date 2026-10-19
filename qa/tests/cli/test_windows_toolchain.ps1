# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
param([string]$SourceFile = 'tools/circuitalive/release/build-windows.ps1')
$ErrorActionPreference = 'Stop'
$projectRoot = (Get-Location).Path
$sourcePath = (Resolve-Path $SourceFile).Path
$source = Get-Content $sourcePath -Raw
$tokens = $null; $parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($sourcePath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
$functions = $ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $false)
# Replace only external operations. Keep and execute the real initialization and phase dispatch.
foreach ($function in ($functions | Sort-Object { $_.Extent.StartOffset } -Descending)) {
    if ($function.Name -eq 'Import-Msvc') {
        $replacement = @'
function Import-Msvc {
    $script:imported = $true
    $env:VCPKG_ROOT = '/visual-studio/bundled-vcpkg'
    $env:SWIG_EXECUTABLE = '/visual-studio/swig'
    $env:VCPKG_DEFAULT_BINARY_CACHE = '/visual-studio/cache'
    $env:VCPKG_BINARY_SOURCES = 'unexpected-feed'
}
'@
    } elseif ($function.Name -eq 'Invoke-Checked') {
        $replacement = 'function Invoke-Checked([string]$Program, [string[]]$Arguments) { Assert-ProjectToolchain; $script:visited.Add("Invoke-Checked: " + $Program) }'
    } else {
        $replacement = 'function ' + $function.Name + ' { Assert-ProjectToolchain; $script:visited.Add("' + $function.Name + '") }'
    }
    $source = $source.Remove($function.Extent.StartOffset, $function.Extent.EndOffset - $function.Extent.StartOffset).Insert($function.Extent.StartOffset, $replacement)
}
# ScriptBlock.Create has no source-file PSScriptRoot. Supply the real paths for the preamble.
$quotedRoot = "'" + $projectRoot.Replace("'", "''") + "'"
$quotedLock = "'" + (Join-Path $projectRoot 'tools/circuitalive/release/windows-toolchain.json').Replace("'", "''") + "'"
$source = $source.Replace('"$PSScriptRoot/../../.."', $quotedRoot).Replace('"$PSScriptRoot/windows-toolchain.json"', $quotedLock)
$program = [scriptblock]::Create($source)
$script:swigVersion = (Get-Content (Join-Path $projectRoot 'tools/circuitalive/release/windows-toolchain.json') -Raw | ConvertFrom-Json).swigVersion
$script:expectedTools = Join-Path $projectRoot '.circuitalive/release-qa/mock-windows-tools'
function Assert-ProjectToolchain {
    if (!$script:imported) { throw 'MSVC environment was not loaded' }
    if ($env:VCPKG_ROOT -ne (Join-Path $script:expectedTools 'vcpkg')) { throw "Developer shell replaced the project vcpkg: $env:VCPKG_ROOT" }
    if ($env:SWIG_EXECUTABLE -ne (Join-Path $script:expectedTools "swigwin-$script:swigVersion/swig.exe")) { throw 'Wrong SWIG path' }
    if ($env:VCPKG_DEFAULT_BINARY_CACHE -ne (Join-Path $script:expectedTools 'binary-cache')) { throw 'Wrong binary cache' }
    if ($env:VCPKG_BINARY_SOURCES -ne 'clear;default') { throw 'Unexpected external binary source' }
}
$saved = @{}
foreach ($name in @('OS','MINGW_PREFIX','VCPKG_ROOT','SWIG_EXECUTABLE','VCPKG_DEFAULT_BINARY_CACHE','VCPKG_BINARY_SOURCES')) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    $env:OS = 'Windows_NT'; Remove-Item Env:MINGW_PREFIX -ErrorAction SilentlyContinue
    foreach ($phase in @('environment','setup','configure','validate','build','package','all')) {
        $script:visited = [System.Collections.Generic.List[string]]::new()
        $script:imported = $false
        & $program -Phase $phase -ToolsRoot $script:expectedTools
        Assert-ProjectToolchain
        if ($phase -eq 'environment') {
            if ($script:visited.Count) { throw 'Environment initialization attempted setup or a build' }
        } elseif (!$script:visited.Count) { throw "No operation was checked for $phase" }
        Write-Output "PASS: $phase ($($script:visited -join ', '))"
    }
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
