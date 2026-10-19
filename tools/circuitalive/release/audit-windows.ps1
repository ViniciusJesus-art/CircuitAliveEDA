# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('inventory', 'setup', 'plan', 'probe-local', 'probe-official', 'collect', 'stop-monitor')]
    [string]$Phase,
    [string]$ToolsRoot = $env:CA_WINDOWS_TOOLS,
    [string]$OutputDirectory = 'build/windows-audit'
)
$ErrorActionPreference = 'Stop'
$ErrorView = 'NormalView'
$PSNativeCommandUseErrorActionPreference = $false # Nonzero CACHE_MISSING is an expected probe result.
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'The native toolchain audit requires Windows.' }
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
Set-Location $root
if (!$ToolsRoot) { $ToolsRoot = Join-Path $env:LOCALAPPDATA 'CircuitAliveEDA/tools' }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
Set-Content "$output/current-phase.txt" $Phase
. "$PSScriptRoot/windows-audit-data.ps1"

function Start-ResourceMonitor {
    Remove-Item "$output/monitor.stop" -ErrorAction SilentlyContinue
    $script = Join-Path $PSScriptRoot 'monitor-windows-runner.ps1'
    $monitor = Start-Process -FilePath (Get-Process -Id $PID).Path -PassThru `
        -ArgumentList @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', "`"$script`"", '-OutputDirectory', "`"$output`"") `
        -RedirectStandardOutput "$output/monitor-stdout.log" -RedirectStandardError "$output/monitor-stderr.log"
    $monitor.Id | Set-Content "$output/monitor.pid"
    Write-Host "Resource monitor started: $($monitor.Id)"
}
function Stop-ResourceMonitor {
    Set-Content "$output/monitor.stop" 'stop'
    if (Test-Path "$output/monitor.pid") {
        $monitor = Get-Process -Id ([int](Get-Content "$output/monitor.pid")) -ErrorAction SilentlyContinue
        if ($monitor -and !$monitor.WaitForExit(10000)) { Write-Warning 'Monitor did not exit within ten seconds.' }
    }
}
if ($Phase -eq 'stop-monitor') { Stop-ResourceMonitor; exit 0 }
if ($Phase -eq 'inventory') {
    Remove-Item "$output/monitor.stop" -ErrorAction SilentlyContinue
    & "$PSScriptRoot/monitor-windows-runner.ps1" -OutputDirectory $output -Once
    $system = Get-CimInstance Win32_ComputerSystem
    $os = Get-CimInstance Win32_OperatingSystem
    [ordered]@{
        timestampUtc = [DateTime]::UtcNow.ToString('o')
        logicalProcessors = $system.NumberOfLogicalProcessors; totalMemoryBytes = [long]$system.TotalPhysicalMemory
        os = $os.Caption; osVersion = $os.Version
        disks = @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' | Select-Object DeviceID, Size, FreeSpace)
        runnerImage = $env:ImageOS; runnerImageVersion = $env:ImageVersion
        pagefiles = @(Get-CimInstance Win32_PageFileUsage | Select-Object Name, AllocatedBaseSize, CurrentUsage, PeakUsage)
        expectedPublicRunner = [ordered]@{ logicalProcessors = 4; memoryGB = 16 }
    } | ConvertTo-Json -Depth 5 | Set-Content "$output/inventory.json"
    Get-Content "$output/inventory.json" | Write-Host
    exit 0
}
if ($Phase -eq 'collect') {
    $cache = Join-Path $ToolsRoot 'binary-cache'
    $inventory = Get-VcpkgCacheInventory $cache
    $inventory | ConvertTo-Json | Set-Content "$output/cache-after.json"
    if ($env:GITHUB_OUTPUT) { "cached-packages=$($inventory.packages)" | Add-Content $env:GITHUB_OUTPUT }
    $vcpkg = Join-Path $ToolsRoot 'vcpkg'
    $logs = "$output/vcpkg-buildtrees"
    New-Item -ItemType Directory -Force $logs | Out-Null
    if (Test-Path "$vcpkg/buildtrees") {
        # Include logs/ABI files, not sources or object trees that can exhaust the artifact budget.
        Get-ChildItem "$vcpkg/buildtrees" -File -Recurse | Where-Object {
            $_.Extension -in @('.log', '.json', '.txt') -and $_.Length -lt 10MB -and
            $_.FullName -notmatch '[\\/](src|x64-windows-rel|x64-windows-dbg)[\\/]'
        } | ForEach-Object {
            $relative = [IO.Path]::GetRelativePath("$vcpkg/buildtrees", $_.FullName)
            $target = Join-Path $logs $relative
            New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
            Copy-Item $_.FullName $target
        }
    }
    try {
        Get-WinEvent -FilterHashtable @{ LogName = 'System'; Id = 2004; StartTime = (Get-Date).AddHours(-2) } -ErrorAction Stop |
            Select-Object TimeCreated, ProviderName, Id, Message | ConvertTo-Json -Depth 4 | Set-Content "$output/resource-exhaustion-events.json"
    } catch { Set-Content "$output/resource-exhaustion-events.txt" $_.Exception.Message }
    Get-ChildItem $output -Filter '*-result.json' | ForEach-Object { Get-Content $_.FullName | Write-Host }
    $summary = @('## Windows audit (no library/application builds)', '', "Binary cache files: $($inventory.packages); bytes: $($inventory.bytes)", '')
    if (Test-Path "$output/inventory.json") {
        $hardware = Get-Content "$output/inventory.json" -Raw | ConvertFrom-Json
        $summary += "Runner: $($hardware.logicalProcessors) logical CPUs, $([math]::Round($hardware.totalMemoryBytes / 1GB, 2)) GiB RAM."
    }
    $samples = @(if (Test-Path "$output/resources.jsonl") {
        Get-Content "$output/resources.jsonl" | ForEach-Object { $_ | ConvertFrom-Json }
    })
    if ($samples.Count) {
        $minFree = ($samples | Measure-Object freeMemoryBytes -Minimum).Minimum
        $maxCpu = ($samples | Measure-Object cpuPercent -Maximum).Maximum
        $summary += "Samples: $($samples.Count); minimum free RAM $([math]::Round($minFree / 1GB, 2)) GiB; peak sampled CPU $maxCpu%."
    } else {
        Write-Warning 'No resource samples survived. Check monitor-errors.log and monitor-stderr.log.'
        $summary += 'No resource samples survived; consult monitor error logs.'
    }
    foreach ($probe in @('plan', 'probe-local', 'probe-official')) {
        if (Test-Path "$output/$probe-result.json") {
            $result = Get-Content "$output/$probe-result.json" -Raw | ConvertFrom-Json
            $summary += "- ${probe}: $($result.outcome); planned $($result.plannedPackages); restored $($result.restoredPackages); missing $($result.missingPackages.Count); elapsed $([math]::Round($result.elapsedSeconds, 1)) s."
        } else { $summary += "- ${probe}: no result (not completed)." }
    }
    $summary += @('', 'A successful cache action alone does not prove ABI cache reuse. See probe logs and command metadata.', 'A lost VM cannot upload diagnostics; earlier checkpoint artifacts remain available.')
    $summary | Set-Content "$output/summary.md"
    if ($env:GITHUB_STEP_SUMMARY) { $summary | Add-Content $env:GITHUB_STEP_SUMMARY }
    exit 0
}

# Keep the monitor's parent alive throughout each long operation. A background
# process launched by a previous Actions step did not survive that step's shell.
Start-ResourceMonitor
try {
if ($Phase -eq 'setup') {
    & "$PSScriptRoot/build-windows.ps1" -Phase setup -ToolsRoot $ToolsRoot 2>&1 | Tee-Object -FilePath "$output/setup.log"
    return
}
# Import MSVC first, then reapply the project's pinned vcpkg environment.
& "$PSScriptRoot/build-windows.ps1" -Phase environment -ToolsRoot $ToolsRoot
$env:VCPKG_MAX_CONCURRENCY = '2'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '2'
$env:VSLANG = '1033'
$env:_CL_ = (($env:_CL_ + ' /MP1').Trim())
$vcpkg = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
if (!(Test-Path $vcpkg)) { throw 'Run tool setup before the audit.' }
if ($Phase -eq 'plan') {
    @(& $vcpkg version; & cmake --version; & ninja --version; & MSBuild -version -nologo; & cl 2>&1) |
        ForEach-Object { "$_" } | Set-Content "$output/tool-versions.log"
}
Get-VcpkgCacheInventory $env:VCPKG_DEFAULT_BINARY_CACHE | ConvertTo-Json | Set-Content "$output/$Phase-cache-before.json"
# Every probe is binary-only; a cache miss must never start a port build.
$arguments = @(Get-VcpkgAuditArguments -Phase $Phase -Root $root)
if ($Phase -eq 'probe-official') {
    $env:VCPKG_BINARY_SOURCES += ';nuget,https://gitlab.com/api/v4/projects/27426693/packages/nuget/index.json,read'
}
[ordered]@{
    timestampUtc = [DateTime]::UtcNow.ToString('o'); phase = $Phase; command = @($vcpkg) + $arguments
    vcpkgMaxConcurrency = $env:VCPKG_MAX_CONCURRENCY; cmakeParallelLevel = $env:CMAKE_BUILD_PARALLEL_LEVEL
    compilerTrackingDisabled = $false; binarySources = $env:VCPKG_BINARY_SOURCES
    compilerVersion = $env:VCToolsVersion; windowsSdkVersion = $env:WindowsSDKVersion
    # vcpkg may compile its tiny compiler/ABI detection probe, never library ports.
    libraryBuildsAllowed = $false
} | ConvertTo-Json -Depth 4 | Set-Content "$output/$Phase-command.json"
$started = Get-Date
& $vcpkg @arguments 2>&1 | Tee-Object -FilePath "$output/$Phase.log"
$exitCode = $LASTEXITCODE
$text = Get-Content "$output/$Phase.log" -Raw
$result = Get-VcpkgAuditResult -Text $text -ExitCode $exitCode
$result | Add-Member NoteProperty elapsedSeconds ((Get-Date) - $started).TotalSeconds
$result | ConvertTo-Json -Depth 6 | Set-Content "$output/$Phase-result.json"
if ($Phase -eq 'plan' -and $exitCode -ne 0) { throw "The install plan failed (exit $exitCode)." }
if ($result.outcome -eq 'error') { throw "The audit encountered an error (exit $exitCode); see $Phase.log." }
if ($result.outcome -eq 'cache-miss') { Write-Host 'Cache misses recorded. No library build was started.' }
# GitHub's PowerShell wrapper propagates LASTEXITCODE even after a handled cache miss.
$global:LASTEXITCODE = 0
} finally { Stop-ResourceMonitor }
