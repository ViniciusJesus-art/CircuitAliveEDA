# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
# Offline regression checks: these never invoke a compiler or vcpkg.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/../../../tools/circuitalive/release/windows-audit-data.ps1"

foreach ($phase in @('plan', 'probe-local', 'probe-official')) {
    $arguments = @(Get-VcpkgAuditArguments -Phase $phase -Root 'C:/project with spaces')
    if ($arguments[0] -ne 'install') { throw 'Unexpected vcpkg command' }
    if ($phase -eq 'plan') {
        if ('--dry-run' -notin $arguments) { throw 'The plan could start a build' }
    } elseif ('--only-binarycaching' -notin $arguments -or '--keep-going' -notin $arguments) {
        throw "The $phase probe could fall back to a build"
    }
    if ('--overlay-triplets=C:/project with spaces/tools/circuitalive/release/triplets' -notin $arguments) {
        throw 'The audit must use the production compiler-tracked triplet'
    }
}
try { Get-VcpkgAuditArguments -Phase build -Root 'C:/test'; throw 'An invalid audit phase was accepted' }
catch [System.Management.Automation.ParameterBindingException] { }
Write-Output 'PASS: plan/probes forbid library builds and keep the project triplet'

$fixture = @'
The following packages will be built and installed:
    boost-algorithm:x64-windows@1.90.0#1 -- git+https://example.org/registry
  * ngspice[codemodels,core]:x64-windows@46 -- git+https://example.org/registry
Restored 1 package(s) from C:/binary-cache in 1 ms. Use --debug to see more details.
  ngspice:x64-windows: CACHE_MISSING: 2.67 us
'@
$result = Get-VcpkgAuditResult $fixture 1
if ($result.outcome -ne 'cache-miss' -or $result.plannedPackages -ne 2 -or
    $result.restoredPackages -ne 1 -or $result.missingPackages.Count -ne 1 -or
    $result.missingPackages[0] -ne 'ngspice' -or $result.ports[0].features -ne '') {
    throw 'Mixed cache hit/miss parsing failed'
}
foreach ($errorLine in @('error: failed to download registry', 'error: unexpected configuration error',
    'warning: NuGet restore failed with HTTP 503', '  ngspice:x64-windows: BUILD_FAILED: 1 s')) {
    if ((Get-VcpkgAuditResult ($fixture + "`n" + $errorLine) 1).outcome -ne 'error') {
        throw "A real failure was hidden by cache misses: $errorLine"
    }
}
if ((Get-VcpkgAuditResult 'unknown failure' 1).outcome -ne 'error' -or
    (Get-VcpkgAuditResult 'Restored 0 package(s) from C:/cache in 1 ms.' 0).outcome -ne 'complete') {
    throw 'Exit status handling failed'
}
Write-Output 'PASS: cache misses remain distinct from configuration, network and build failures'

$nugetFixture = $fixture + @'

warning: "C:\tools\nuget.exe" install packages.config -Source https://example.org/index.json failed with exit code 1
  GET https://example.org/missing.nupkg
  NotFound https://example.org/missing.nupkg 123ms
'@
if ((Get-VcpkgAuditResult $nugetFixture 1).outcome -ne 'cache-miss') { throw 'Expected NuGet package 404 was treated as an error' }
foreach ($badResponse in @('  GET https://example.org/no-response.nupkg',
    '  ServiceUnavailable https://example.org/other.nupkg 200ms', 'error: unexpected NuGet configuration',
    '  Unauthorized https://example.org/private.nupkg 100ms')) {
    if ((Get-VcpkgAuditResult ($nugetFixture + "`n" + $badResponse) 1).outcome -ne 'error') {
        throw "Unexpected NuGet failure was hidden: $badResponse"
    }
}
Write-Output 'PASS: NuGet missing-package warning is accepted only with accounted-for responses and no real errors'

$directory = Join-Path ([IO.Path]::GetTempPath()) ('ca-cache-qa-' + [guid]::NewGuid())
try {
    if ((Get-VcpkgCacheInventory $directory).files -ne 0) { throw 'Missing cache is not empty' }
    New-Item -ItemType Directory $directory | Out-Null
    Set-Content "$directory/a.zip" 'test'
    if ((Get-VcpkgCacheInventory $directory).packages -ne 1) { throw 'Single package count failed' }
    Set-Content "$directory/metadata.txt" 'metadata'
    $inventory = Get-VcpkgCacheInventory $directory
    if ($inventory.files -ne 2 -or $inventory.packages -ne 1 -or $inventory.bytes -le 0) { throw 'Cache inventory failed' }
} finally { Remove-Item $directory -Recurse -Force -ErrorAction SilentlyContinue }
Write-Output 'PASS: absent, single-file and mixed cache inventories'

$directory = Join-Path ([IO.Path]::GetTempPath()) ('ca-log-qa-' + [guid]::NewGuid())
try {
    New-Item -ItemType Directory -Force "$directory/gperf/x64-windows-rel/lib", "$directory/gperf/src" | Out-Null
    Set-Content "$directory/gperf/x64-windows-rel/lib/config.log" 'nested compiler error'
    Set-Content "$directory/gperf/x64-windows-rel/lib/conftest.obj" 'object'
    Set-Content "$directory/gperf/src/irrelevant.txt" 'source'
    if (!(Test-WindowsDiagnosticFile (Get-Item "$directory/gperf/x64-windows-rel/lib/config.log")) -or
        (Test-WindowsDiagnosticFile (Get-Item "$directory/gperf/x64-windows-rel/lib/conftest.obj")) -or
        (Test-WindowsDiagnosticFile (Get-Item "$directory/gperf/src/irrelevant.txt"))) {
        throw 'The collector must keep nested configure errors without archiving sources/objects'
    }
} finally { Remove-Item $directory -Recurse -Force -ErrorAction SilentlyContinue }
Write-Output 'PASS: nested Autoconf logs survive failure diagnostics'

Get-ChildItem "$PSScriptRoot/../../../tools/circuitalive/release" -Filter '*windows*.ps1' | ForEach-Object {
    $tokens = $null; $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($_.FullName, [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw ($errors | Out-String) }
}
Write-Output 'PASS: Windows script syntax'
