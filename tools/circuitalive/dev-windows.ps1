# Native Windows development; all builds share the same sources as Fedora.
[CmdletBinding()]
param(
    [ValidateSet('doctor', 'prepare', 'build', 'validate', 'libraries', 'run', 'package')]
    [string]$Phase = 'doctor',
    [string]$Version = '10.0.7-alpha.4.1',
    [ValidateSet('kicad', 'eeschema', 'pcbnew')][string]$Application = 'kicad',
    [string]$File,
    [ValidateRange(1, 2)][int]$Jobs = 2,
    [string]$ToolsRoot = $env:CA_WINDOWS_TOOLS
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$builder = Join-Path $PSScriptRoot 'release/build-windows.ps1'
$stage = Join-Path $root 'stage/windows-msvc'
$python = Join-Path $root 'build/windows-msvc/vcpkg_installed/x64-windows/tools/python3/python.exe'
if (!$ToolsRoot) { $ToolsRoot = Join-Path $root '.circuitalive/windows-tools' }

function Invoke-BuildPhase([string]$Name) {
    & $builder -Phase $Name -Version $Version -Jobs $Jobs -ToolsRoot $ToolsRoot
}

if ($Phase -eq 'doctor') {
    Write-Output "Checkout: $root"
    Write-Output "Tools and cache: $ToolsRoot"
    Write-Output "PowerShell: $($PSVersionTable.PSVersion) (7+ required)"
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vs = if (Test-Path $vswhere) {
        & $vswhere -latest -products '*' -version '[17.0,18.0)' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if ($vs) { Write-Output "MSVC 2022: $vs" }
    else { Write-Output 'MSVC 2022: MISSING (install Build Tools with the C++ workload)' }
    $dependenciesReady = $false
    if ($PSVersionTable.PSVersion.Major -ge 7) {
        . "$PSScriptRoot/release/windows-dependencies-data.ps1"
        try { Assert-WindowsDependenciesComplete $root; $dependenciesReady = $true } catch { }
    }
    Write-Output "Dependencies ready: $dependenciesReady"
    Write-Output "Application staged: $(Test-Path "$stage/bin/CircuitAliveEDA.exe")"
    return
}
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Run this script with PowerShell 7 (pwsh).' }
if ($env:OS -ne 'Windows_NT') { throw 'Use native Windows PowerShell.' }

switch ($Phase) {
    prepare {
        Invoke-BuildPhase 'setup'
        Invoke-BuildPhase 'dependencies'
        Invoke-BuildPhase 'configure'
    }
    build {
        Invoke-BuildPhase 'build'
        Invoke-BuildPhase 'stage'
    }
    validate { Invoke-BuildPhase 'validate' }
    libraries {
        Invoke-BuildPhase 'environment'
        if (!(Test-Path $python)) { throw 'Prepare the Windows dependencies first (-Phase prepare).' }
        & $python "$PSScriptRoot/release/prepare-source-libraries.py" `
            --directory "$root/.circuitalive/libraries/share/kicad"
        if ($LASTEXITCODE -ne 0) { throw 'Component library preparation failed.' }
    }
    run {
        $executable = Join-Path $stage "bin/$Application.exe"
        if (!(Test-Path $executable)) { throw 'Build and stage the application first (-Phase build).' }
        $data = Join-Path $root '.circuitalive/libraries/share/kicad'
        if (!(Test-Path "$data/symbols")) { throw 'Prepare the component libraries first (-Phase libraries).' }
        $env:KICAD10_SYMBOL_DIR = Join-Path $data 'symbols'
        $env:KICAD10_FOOTPRINT_DIR = Join-Path $data 'footprints'
        $env:KICAD10_3DMODEL_DIR = Join-Path $data '3dmodels'
        $env:KICAD10_TEMPLATE_DIR = Join-Path $data 'template'
        $env:CIRCUITALIVE_MODEL_DIR = Join-Path $stage 'share/circuitalive/models'
        $env:CIRCUITALIVE_BLOCK_DIR = Join-Path $stage 'share/circuitalive/blocks'
        $env:PYTHONHOME = Join-Path $stage 'bin'
        & $python "$PSScriptRoot/release/prepare-runtime.py" "$stage/bin" "$stage/lib/ngspice"
        if ($LASTEXITCODE -ne 0) { throw 'Simulation runtime preparation failed.' }
        $env:SPICE_SCRIPTS = Join-Path $stage 'bin/ngspice/scripts'
        $arguments = @()
        if ($File) { $arguments += '"' + (Resolve-Path -LiteralPath $File).Path + '"' }
        # Launch the staged native application, with its DLLs and runtime beside it.
        # The development library directory avoids copying several GB on each edit.
        $launch = @{ FilePath = $executable; WorkingDirectory = "$stage/bin"; PassThru = $true }
        if ($arguments.Count) { $launch.ArgumentList = $arguments }
        Start-Process @launch | Select-Object Id, ProcessName
    }
    package {
        & "$PSScriptRoot/release/prepare-inno.ps1" -ToolsRoot $ToolsRoot
        Invoke-BuildPhase 'package'
    }
}
