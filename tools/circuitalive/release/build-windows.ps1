[CmdletBinding()]
param(
    [string]$Version = '10.0.7-alpha.1',
    [ValidateSet('all', 'environment', 'setup', 'dependencies', 'configure', 'validate', 'build', 'stage', 'package')]
    [string]$Phase = 'all',
    [ValidateRange(1, 2)][int]$Jobs = 2,
    [string]$ToolsRoot = $env:CA_WINDOWS_TOOLS
)
$ErrorActionPreference = 'Stop'
$ErrorView = 'NormalView'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'This build requires native Windows and Visual Studio 2022.' }
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[a-zA-Z0-9.]+)?$') { throw 'Invalid release version.' }
if ($env:MINGW_PREFIX) { throw 'Run in native PowerShell, outside the MSYS2 environment.' }
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
Set-Location $root
if (!$ToolsRoot) { $ToolsRoot = Join-Path $env:LOCALAPPDATA 'CircuitAliveEDA/tools' }
$lock = Get-Content "$PSScriptRoot/windows-toolchain.json" -Raw | ConvertFrom-Json
$build = Join-Path $root 'build/windows-msvc'
$prefix = Join-Path $build 'vcpkg_installed/x64-windows'
$python = Join-Path $prefix 'tools/python3/python.exe'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE): $Arguments" }
}

function Import-Msvc {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'Visual Studio 2022 Desktop development with C++ is required.' }
    Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
    foreach ($tool in @('cl.exe', 'cmake.exe', 'ninja.exe', 'rc.exe', 'git.exe')) {
        Get-Command $tool -ErrorAction Stop | Out-Null
    }
}

function Initialize-Tools {
    New-Item -ItemType Directory -Force $ToolsRoot, $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
    if (!(Test-Path "$env:VCPKG_ROOT/.git")) {
        Invoke-Checked -Program git -Arguments @('clone', 'https://github.com/microsoft/vcpkg.git', $env:VCPKG_ROOT)
        Invoke-Checked -Program git -Arguments @('-C', $env:VCPKG_ROOT, 'checkout', '--detach', $lock.vcpkgCommit)
    }
    $actual = & git -C $env:VCPKG_ROOT rev-parse HEAD
    if ($actual -ne $lock.vcpkgCommit) { throw "Use a vcpkg clone pinned to $($lock.vcpkgCommit): $env:VCPKG_ROOT" }
    Invoke-Checked -Program "$env:VCPKG_ROOT/bootstrap-vcpkg.bat" -Arguments @('-disableMetrics')
    . "$PSScriptRoot/limit-windows-toolchain.ps1"
    Set-LegacyMsbuildLimit -Path "$env:VCPKG_ROOT/scripts/cmake/vcpkg_build_msbuild.cmake" -OriginalSha256 $lock.legacyMsbuildHelperSha256
    if (!(Test-Path $env:SWIG_EXECUTABLE)) {
        & "$PSScriptRoot/prepare-swig.ps1" -ToolsRoot $ToolsRoot `
            -Version $lock.swigVersion -Sha256 $lock.swigSha256
    }
    Invoke-Checked -Program $env:SWIG_EXECUTABLE -Arguments @('-version')
}

function Prepare-Dependencies {
    & "$PSScriptRoot/prepare-windows-dependencies.ps1" -ToolsRoot $ToolsRoot -Jobs $Jobs
}

function Configure-Project {
    if (!(Test-Path $env:SWIG_EXECUTABLE)) { throw 'Run -Phase setup first.' }
    . "$PSScriptRoot/windows-dependencies-data.ps1"
    Assert-WindowsDependenciesComplete -Root $root
    Invoke-Checked -Program cmake -Arguments @('--preset', 'circuitalive-windows-msvc', "-DCIRCUITALIVE_RELEASE_VERSION=$Version")
    $compilerFile = Get-ChildItem "$build/CMakeFiles/*/CMakeCXXCompiler.cmake" | Select-Object -First 1
    if (!(Select-String -Path $compilerFile.FullName -Pattern 'CMAKE_CXX_COMPILER_ID "MSVC"' -Quiet)) {
        throw 'The selected compiler is not MSVC. Use a fresh build/windows-msvc directory.'
    }
}

function Validate-Project {
    # Compile the affected controller, worker, tests and GUI consumers before the full build.
    $targets = @('qa_interactive_sim', 'qa_circuitalive_json_link',
        'eeschema/CMakeFiles/eeschema_kiface_objects.dir/circuitalive/instrument_panel.cpp.obj',
        'eeschema/CMakeFiles/eeschema_kiface_objects.dir/circuitalive/simulation_overlay.cpp.obj')
    Invoke-Checked -Program cmake -Arguments (@('--build', $build, '--parallel', "$Jobs", '--target') + $targets)
    Invoke-Checked -Program $python -Arguments @("$PSScriptRoot/prepare-runtime.py", "$build/eeschema/sim", "$prefix/lib/ngspice")
    $env:SPICE_SCRIPTS = "$build/eeschema/sim/ngspice/scripts"
    $env:PATH = "$prefix/bin;$prefix/tools/python3;$build/common;$build/api;$build/common/gal;$env:PATH"
    Invoke-Checked -Program ctest -Arguments @('--test-dir', $build, '-R', '^qa_(interactive_sim|circuitalive_json_link)$', '--output-on-failure')
}

Import-Msvc
# Developer PowerShell replaces VCPKG_ROOT with Visual Studio's bundled copy.
# Apply our pinned toolchain paths after importing it, for every build phase.
$env:VCPKG_ROOT = Join-Path $ToolsRoot 'vcpkg'
$env:SWIG_EXECUTABLE = Join-Path $ToolsRoot "swigwin-$($lock.swigVersion)/swig.exe"
$env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $ToolsRoot 'binary-cache'
# Compiler-tracked local cache only: no MinGW or untracked feed packages.
$env:VCPKG_BINARY_SOURCES = 'clear;default,readwrite'
$env:VCPKG_MAX_CONCURRENCY = "$Jobs"
$env:CMAKE_BUILD_PARALLEL_LEVEL = "$Jobs"
$env:_CL_ = '/MP1'
# Autotools ports invoke native cl.exe through MSYS2. /MP1 is an option,
# not a POSIX path to convert when exporting the native process environment.
$env:MSYS2_ENV_CONV_EXCL = '_CL_'
Write-Host "Using project vcpkg: $env:VCPKG_ROOT"
if ($Phase -in @('all', 'setup')) { Initialize-Tools }
if ($Phase -in @('all', 'dependencies')) { Prepare-Dependencies }
if ($Phase -in @('all', 'configure')) { Configure-Project }
if ($Phase -in @('all', 'validate', 'build')) { Validate-Project }
if ($Phase -in @('all', 'build')) {
    Invoke-Checked -Program cmake -Arguments @('--build', '--preset', 'circuitalive-windows-msvc', '--parallel', "$Jobs")
}
if ($Phase -in @('all', 'stage', 'package')) {
    Invoke-Checked -Program cmake -Arguments @('--install', $build)
    Invoke-Checked -Program $python -Arguments @("$PSScriptRoot/package-windows.py", "$root/stage/windows-msvc", $prefix, '--version', $Version, '--prepare-only')
}
if ($Phase -in @('all', 'package')) {
    Invoke-Checked -Program $python -Arguments @("$PSScriptRoot/package-windows.py", "$root/stage/windows-msvc", '--version', $Version)
}
