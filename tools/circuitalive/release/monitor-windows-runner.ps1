# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(1, 300)][int]$IntervalSeconds = 30,
    [switch]$Once
)
$ErrorActionPreference = 'Stop'
$ErrorView = 'NormalView'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'Resource sampling requires Windows.' }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path
$stopFile = Join-Path $OutputDirectory 'monitor.stop'
$samples = Join-Path $OutputDirectory 'resources.jsonl'
while (!(Test-Path $stopFile)) {
    try {
        $os = Get-CimInstance Win32_OperatingSystem
        $cpu = Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'"
        $memory = Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory
        $disks = @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' | ForEach-Object {
            [ordered]@{ volume = $_.DeviceID; freeBytes = [long]$_.FreeSpace; sizeBytes = [long]$_.Size }
        })
        $processes = @(Get-CimInstance Win32_Process | Where-Object {
            $_.Name -match '^(cl|link|msbuild|ninja|python[0-9]*|cmake|vcpkg)\.exe$'
        } | ForEach-Object {
            [ordered]@{
                name = $_.Name; pid = $_.ProcessId; parentPid = $_.ParentProcessId
                workingSetBytes = [long]$_.WorkingSetSize; privateBytes = [long]$_.PrivatePageCount
                cpuSeconds = ([double]$_.KernelModeTime + [double]$_.UserModeTime) / 10000000
            }
        })
        $sample = [ordered]@{
            timestampUtc = [DateTime]::UtcNow.ToString('o')
            phase = if (Test-Path "$OutputDirectory/current-phase.txt") { (Get-Content "$OutputDirectory/current-phase.txt" -Raw).Trim() } else { 'startup' }
            cpuPercent = [double]$cpu.PercentProcessorTime
            totalMemoryBytes = [long]$os.TotalVisibleMemorySize * 1024
            freeMemoryBytes = [long]$os.FreePhysicalMemory * 1024
            committedBytes = [long]$memory.CommittedBytes; commitLimitBytes = [long]$memory.CommitLimit
            disks = $disks; processes = $processes
        }
        # Close each write immediately; partial diagnostics survive an ordinary failure.
        [IO.File]::AppendAllText($samples, (($sample | ConvertTo-Json -Depth 6 -Compress) + "`n"))
        if ($sample.freeMemoryBytes -lt 2GB -or $sample.committedBytes -gt 0.9 * $sample.commitLimitBytes -or $disks.Where({ $_.freeBytes -lt 5GB }).Count) {
            [IO.File]::AppendAllText("$OutputDirectory/resource-warnings.log", "$($sample.timestampUtc) Low RAM or disk headroom; see resources.jsonl`n")
        }
    } catch {
        [IO.File]::AppendAllText("$OutputDirectory/monitor-errors.log", "$([DateTime]::UtcNow.ToString('o')) $($_.Exception.Message)`n")
    }
    if ($Once) { break }
    for ($second = 0; $second -lt $IntervalSeconds -and !(Test-Path $stopFile); $second++) {
        Start-Sleep -Seconds 1
    }
}
