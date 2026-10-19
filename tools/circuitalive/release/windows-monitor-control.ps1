# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
# The caller supplies $output and keeps this parent shell alive until Stop.
function Start-ResourceMonitor {
    Remove-Item "$output/monitor.stop" -ErrorAction SilentlyContinue
    $script = Join-Path $PSScriptRoot 'monitor-windows-runner.ps1'
    $monitor = Start-Process -FilePath (Get-Process -Id $PID).Path -PassThru -NoNewWindow `
        -ArgumentList @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', "`"$script`"", '-OutputDirectory', "`"$output`"") `
        -RedirectStandardError "$output/monitor-stderr.log"
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
