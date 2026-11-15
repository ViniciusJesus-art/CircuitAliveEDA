# Exercise the actual MSVC/cache combination before the expensive application build.
param([string]$BuildDirectory = "$PSScriptRoot/../../../build/windows-msvc")
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version Latest
Get-Command sccache, cl.exe -CommandType Application -ErrorAction Stop | Out-Null
$configuration = Get-Content "$BuildDirectory/CMakeCache.txt" -Raw
foreach ($language in @('C', 'CXX')) {
    if ($configuration -notmatch "(?m)^CMAKE_${language}_COMPILER_LAUNCHER:[^=]+=.+sccache") {
        throw "The $language application compiler launcher is not configured"
    }
}
$commands = Get-Content "$BuildDirectory/compile_commands.json" -Raw | ConvertFrom-Json
$cacheFlags = @()
foreach ($source in @('pcbnew/pcb_painter.cpp', 'eeschema/widgets/panel_symbol_chooser.cpp',
    'eeschema/circuitalive/live_control_popup.cpp', 'eeschema/sim/interactive_sim_worker.cpp')) {
    $entries = @($commands | Where-Object { $_.file.Replace('\', '/').EndsWith($source) })
    if (!$entries.Count) { throw "Missing application compiler command: $source" }
    foreach ($entry in $entries) {
        if ($entry.command -cmatch '(?:^|\s)[/-](?:Zi|ZI)(?:\s|$)|(?:^|\s)[/-](?:Yc|Yu|Fp)' -or
            $entry.command -notmatch '(?:^|\s)[/-]Z7(?:\s|$)') {
            throw "The MSVC command cannot cache independent objects: $source"
        }
        if ($entry.command -match '(?:^|\s)/external:env:') {
            throw "sccache interprets /external:env as an extra input file: $source"
        }
        # Exercise the warning option from application commands as well as /Z7.
        $cacheFlags += @([regex]::Matches($entry.command, '(?:^|\s)(-external:env:[^\s"]+)') |
            ForEach-Object { $_.Groups[1].Value })
    }
}
$cacheFlags = @($cacheFlags | Sort-Object -Unique)
function Read-CacheStats {
    $json = & sccache --show-stats --stats-format=json
    if ($LASTEXITCODE -ne 0) {
        Write-Warning 'Compiler cache statistics unavailable; keep the compiler fallback enabled.'
        return $null
    }
    return ($json | ConvertFrom-Json).stats
}
$directory = Join-Path ([IO.Path]::GetTempPath()) ('ca-compiler-cache-' + [guid]::NewGuid())
New-Item -ItemType Directory $directory | Out-Null
try {
    foreach ($extension in @('c', 'cpp')) {
        $source = Join-Path $directory "smoke.$extension"
        $header = Join-Path $directory 'value.h'
        $object = Join-Path $directory "smoke-$extension.obj"
        $binary = Join-Path $directory "smoke-$extension.exe"
        Set-Content $source "#include `"value.h`"`nint main(void) { return CA_VALUE; }"
        foreach ($expected in @(7, 7, 11)) {
            # Repeat the unmodified input, then change a header at the same path.
            Set-Content $header "#define CA_VALUE $expected"
            $before = Read-CacheStats
            & sccache cl.exe /nologo /c /Z7 @cacheFlags $source "/Fo$object"
            if ($LASTEXITCODE -ne 0) { throw "Cached $extension compilation failed" }
            $after = Read-CacheStats
            if ($null -ne $before -and $null -ne $after -and
                $after.requests_not_cacheable -gt $before.requests_not_cacheable) {
                throw "sccache bypassed the $extension compilation with application warning options"
            }
            & cl.exe /nologo $object "/Fe$binary"
            if ($LASTEXITCODE -ne 0) { throw "Cached $extension object could not be linked" }
            & $binary
            if ($LASTEXITCODE -ne $expected) { throw 'Compiler cache reused stale header contents' }
        }
    }
    & sccache --show-stats
    Write-Output 'PASS: cached MSVC C/C++ objects link and reflect header changes'
} finally {
    Remove-Item $directory -Recurse -Force
}
exit 0 # Cache telemetry is optional; compiler/link/header errors above still throw.
