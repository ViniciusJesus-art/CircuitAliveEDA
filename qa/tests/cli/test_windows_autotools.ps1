# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
# Native integration check after the small gperf installation, before heavy ports.
[CmdletBinding()]
param([string]$OutputDirectory = 'build/windows-dependencies')
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'This integration check requires native Windows/MSVC.' }
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
Set-Location $root
& "$root/tools/circuitalive/release/build-windows.ps1" -Phase environment
. "$root/tools/circuitalive/release/windows-dependencies-data.ps1"
. "$root/tools/circuitalive/release/windows-audit-data.ps1"
$output = (Resolve-Path $OutputDirectory).Path
# A binary-cache hit does not acquire the port's build-only MSYS2 runtime.
# Prefer that runtime; otherwise use an explicit MSYS shell from the runner.
# Never resolve a generic bash.exe, which could be the unrelated WSL launcher.
$shell = $null
foreach ($candidate in @("$env:VCPKG_ROOT/downloads/tools/msys2/*/usr/bin/bash.exe",
    'C:/msys64/usr/bin/bash.exe', "${env:ProgramFiles}/Git/usr/bin/bash.exe")) {
    $shell = Get-ChildItem $candidate -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($shell) { break }
}
if (!$shell) { throw 'An MSYS shell is required for the native regression check.' }
$probe = Join-Path $output 'msys-native-cl-probe.ps1'
@'
param([string]$Directory)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
New-Item -ItemType Directory -Force $Directory | Out-Null
Set-Location $Directory
Set-Content cl-probe.c 'int main(void) { return 0; }' -Encoding ascii
& cl /nologo cl-probe.c /Fecl-probe.exe 2>&1 | Set-Content compile.log
$compilerExitCode = $LASTEXITCODE
$runExitCode = $null
if ($compilerExitCode -eq 0) { & ./cl-probe.exe; $runExitCode = $LASTEXITCODE }
@{ flags = $env:_CL_; compilerExitCode = $compilerExitCode; runExitCode = $runExitCode } |
    ConvertTo-Json | Set-Content result.json
$global:LASTEXITCODE = 0
'@ | Set-Content $probe

$savedExclusions = $env:MSYS2_ENV_CONV_EXCL
try {
    $results = @{ shell = $shell.FullName }
    foreach ($case in @('baseline', 'protected')) {
        $directory = Join-Path $output "msys-$case"
        $env:MSYS2_ENV_CONV_EXCL = if ($case -eq 'baseline') { $null } else { '_CL_' }
        $nativePowerShell = (Get-Process -Id $PID).Path.Replace('\', '/')
        $command = '"{0}" -NoLogo -NoProfile -NonInteractive -File "{1}" -Directory "{2}"' -f `
            $nativePowerShell, $probe.Replace('\', '/'), $directory.Replace('\', '/')
        & $shell.FullName --noprofile --norc -c $command
        if ($LASTEXITCODE -ne 0) { throw "The MSYS2/native probe itself failed: $case" }
        $results[$case] = Get-Content "$directory/result.json" -Raw | ConvertFrom-Json
    }
    if ($results.protected.flags -ne '/MP1' -or $results.protected.compilerExitCode -ne 0 -or
        $results.protected.runExitCode -ne 0) { throw 'MSYS2 altered MSVC flags, or the protected native compiler probe failed.' }
    $results | ConvertTo-Json -Depth 5 | Set-Content "$output/msys-cl-regression.json"
    Write-Host "MSYS2 baseline flags: $($results.baseline.flags); compiler exit: $($results.baseline.compilerExitCode)"
    Write-Host 'PASS: protected /MP1 survives the actual MSYS2 shell and a native C executable compiles and runs'
} finally { $env:MSYS2_ENV_CONV_EXCL = $savedExclusions }

$cache = Get-VcpkgCacheInventory $env:VCPKG_DEFAULT_BINARY_CACHE
if ($cache.packages -eq 0) { throw 'No binary packages were written. Refuse heavy dependency builds with a non-working cache.' }
# Use a fresh install directory to prove actual restoration, not an already-installed no-op.
$verificationInstall = Join-Path $root ("build/windows-msvc/cache-verification-" + [guid]::NewGuid())
$arguments = @(Get-WindowsDependencyArguments -Root $root -ManifestDirectory "$output/manifests/gperf" -Plan) |
    Where-Object { $_ -ne '--dry-run' } | ForEach-Object {
        if ($_.StartsWith('--x-install-root=')) { "--x-install-root=$verificationInstall" } else { $_ }
    }
$arguments += '--only-binarycaching' # Never rebuild in this verification step.
@{ command = @("$env:VCPKG_ROOT/vcpkg.exe") + $arguments; libraryBuildsAllowed = $false } |
    ConvertTo-Json -Depth 4 | Set-Content "$output/preflight-cache-command.json"
& "$env:VCPKG_ROOT/vcpkg.exe" @arguments 2>&1 | Tee-Object -FilePath "$output/preflight-cache-verification.log"
$exitCode = $LASTEXITCODE
$result = Get-VcpkgAuditResult -Text (Get-Content "$output/preflight-cache-verification.log" -Raw) -ExitCode $exitCode
$result | ConvertTo-Json -Depth 6 | Set-Content "$output/preflight-cache-result.json"
if ($exitCode -ne 0 -or $result.restoredPackages -eq 0) { throw 'The binary-only restore to a fresh install directory failed.' }
Write-Host "PASS: $($result.restoredPackages) packages restored from the cache written by the gperf preflight"
$global:LASTEXITCODE = 0
