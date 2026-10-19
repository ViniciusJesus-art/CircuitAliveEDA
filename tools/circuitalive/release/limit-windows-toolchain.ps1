# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
function Set-LegacyMsbuildLimit {
    param([string]$Path, [string]$OriginalSha256)
    $text = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $unbounded = '"/m"'
    $bounded = '"/m:${VCPKG_CONCURRENCY}"'
    $original = $text.Replace($bounded, $unbounded)
    $digest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($original))).ToLowerInvariant()
    if ($digest -ne $OriginalSha256 -or ([regex]::Matches($original, [regex]::Escape($unbounded))).Count -ne 1) {
        throw 'Unexpected legacy vcpkg MSBuild helper; review the pinned toolchain before changing it.'
    }
    [IO.File]::WriteAllText($Path, $original.Replace($unbounded, $bounded), [Text.UTF8Encoding]::new($false))
}
