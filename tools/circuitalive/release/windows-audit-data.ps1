# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
# Pure log parsing shared by the audit and its offline regression tests.
function Get-VcpkgAuditArguments {
    param([ValidateSet('plan', 'probe-local', 'probe-official')][string]$Phase, [string]$Root)
    $arguments = @('install', '--triplet=x64-windows', '--host-triplet=x64-windows',
        "--overlay-triplets=$Root/tools/circuitalive/release/triplets",
        "--x-install-root=$Root/build/windows-msvc/vcpkg_installed")
    if ($Phase -eq 'plan') { $arguments += '--dry-run' }
    else { $arguments += @('--only-binarycaching', '--keep-going') }
    return $arguments
}

function Get-VcpkgAuditResult {
    param([string]$Text, [int]$ExitCode)
    $ports = @($Text -split "`r?`n" | ForEach-Object {
        if ($_ -match '^\s+(?:\*\s+)?(?<name>[a-z0-9-]+)(?<features>\[[^]]+\])?:x64-windows@(?<version>\S+)') {
            [pscustomobject]@{ name = $Matches.name; features = [string]$Matches['features']; version = $Matches.version }
        }
    } | Sort-Object name -Unique)
    $hasMisses = $Text -match 'CACHE_MISSING|There are packages that have not been built'
    # NuGet returns 1 even when every request received an ordinary package 404.
    # Ignore only that known wrapper warning, with a complete set of responses.
    $requests = @([regex]::Matches($Text, '(?m)^\s+GET (?<url>https://\S+\.nupkg)\s*$') | ForEach-Object { $_.Groups['url'].Value } | Sort-Object -Unique)
    $responses = @([regex]::Matches($Text, '(?m)^\s+(?:NotFound|OK) (?<url>https://\S+\.nupkg) [^\r\n]+') | ForEach-Object { $_.Groups['url'].Value } | Sort-Object -Unique)
    $accountedFor = $requests.Count -gt 0 -and $requests.Count -eq $responses.Count -and
        @($requests | Where-Object { $_ -notin $responses }).Count -eq 0
    $diagnosticText = if ($accountedFor) {
        $Text -replace '(?m)^warning: "[^"\r\n]*nuget\.exe" install [^\r\n]+ failed with exit code 1\r?$', ''
    } else { $Text }
    $hasFailure = $diagnosticText -match '(?im)BUILD_FAILED|POST_BUILD_CHECKS_FAILED|FILE_CONFLICTS|^error:|^warning:.*?(?:failed|unable|could not|HTTP [45])|Unauthorized|Forbidden|ServiceUnavailable|InternalServerError|BadGateway|GatewayTimeout|TooManyRequests|timed out|unable to load|HTTP [45]\d\d'
    $missing = @([regex]::Matches($Text, '(?m)^\s*([a-z0-9-]+):x64-windows: CACHE_MISSING:.*$') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    $restored = @([regex]::Matches($Text, '(?m)^Restored (\d+) package.*$') | ForEach-Object { [int]$_.Groups[1].Value })
    [pscustomobject]@{
        exitCode = $ExitCode
        outcome = if ($hasFailure) { 'error' } elseif ($ExitCode -eq 0) { 'complete' } elseif ($hasMisses) { 'cache-miss' } else { 'error' }
        ports = $ports
        plannedPackages = $ports.Count
        missingPackages = $missing
        restoredPackages = if ($restored.Count) { [int]($restored | Measure-Object -Sum).Sum } else { 0 }
        restoredMessages = @([regex]::Matches($Text, '(?m)^.*Restored \d+ package.*$') | ForEach-Object { $_.Value.Trim() })
        missingMessages = @([regex]::Matches($Text, '(?m)^.*CACHE_MISSING.*$') | ForEach-Object { $_.Value.Trim() })
    }
}

function Get-VcpkgCacheInventory {
    param([string]$Path)
    $files = @(if (Test-Path $Path) { Get-ChildItem $Path -Recurse -File })
    [pscustomobject]@{
        path = $Path; files = $files.Count
        bytes = if ($files.Count) { [long]($files | Measure-Object Length -Sum).Sum } else { 0L }
        packages = @($files | Where-Object Extension -eq '.zip').Count
    }
}
