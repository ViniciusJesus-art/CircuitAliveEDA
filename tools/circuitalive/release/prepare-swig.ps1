# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ToolsRoot,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$Sha256
)
$ErrorActionPreference = 'Stop'
$ErrorView = 'NormalView'
Set-StrictMode -Version Latest
New-Item -ItemType Directory -Force $ToolsRoot | Out-Null
$ToolsRoot = (Resolve-Path $ToolsRoot).Path
$archive = Join-Path $ToolsRoot "swigwin-$Version.zip"
$partial = "$archive.part"

# SourceForge's /download page can return HTML with HTTP 200. Verify the exact
# swigwin archive against the checksum used by the official KiCad Windows builder.
$verified = (Test-Path $archive) -and ((Get-FileHash $archive -Algorithm SHA256).Hash -eq $Sha256)
if (!$verified) {
    $curl = if ($env:OS -eq 'Windows_NT') { 'curl.exe' } else { 'curl' }
    Get-Command $curl -CommandType Application -ErrorAction Stop | Out-Null
    $urls = @(
        "https://pilotfiber.dl.sourceforge.net/project/swig/swigwin/swigwin-$Version/swigwin-$Version.zip",
        "https://downloads.sourceforge.net/project/swig/swigwin/swigwin-$Version/swigwin-$Version.zip",
        "https://sourceforge.net/projects/swig/files/swigwin/swigwin-$Version/swigwin-$Version.zip/download?use_mirror=pilotfiber"
    )
    $failures = [System.Collections.Generic.List[string]]::new()
    foreach ($url in $urls) {
        try {
            Write-Host "Downloading SWIG $Version from $url"
            & $curl --fail --location --silent --show-error --retry 2 --retry-delay 2 `
                --connect-timeout 15 --max-time 120 --output $partial $url
            if ($LASTEXITCODE -ne 0) { throw "curl exited with code $LASTEXITCODE" }
            $actual = (Get-FileHash $partial -Algorithm SHA256).Hash
            if ($actual -ne $Sha256) {
                throw "SWIG archive checksum mismatch: expected $Sha256, received $actual (possibly an HTML download page)"
            }
            Move-Item $partial $archive -Force
            $verified = $true
            break
        } catch {
            $failures.Add("${url}: $($_.Exception.Message)")
            Write-Warning $failures[$failures.Count - 1]
        } finally {
            if (Test-Path $partial) { Remove-Item $partial -Force }
        }
    }
    if (!$verified) { throw "Cannot download the verified SWIG archive:`n$($failures -join "`n")" }
}

Write-Host "Verified SWIG $Version SHA256: $Sha256"
try {
    [System.IO.Compression.ZipFile]::ExtractToDirectory($archive, $ToolsRoot, $true)
} catch {
    throw "Cannot extract the verified SWIG archive ${archive}: $($_.Exception.Message)"
}
foreach ($file in @('swig.exe', 'Lib/swig.swg')) {
    $expected = Join-Path $ToolsRoot "swigwin-$Version/$file"
    if (!(Test-Path $expected -PathType Leaf)) { throw "Incomplete SWIG installation: missing $expected" }
}
