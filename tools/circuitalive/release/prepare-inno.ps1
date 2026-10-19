# Use the native x64 compiler: the complete CAD libraries exceed NSIS's limits.
[CmdletBinding()]
param([string]$ToolsRoot = $env:CA_WINDOWS_TOOLS)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'Inno Setup preparation requires Windows.' }
if (!$ToolsRoot) { $ToolsRoot = Join-Path $env:LOCALAPPDATA 'CircuitAliveEDA/tools' }
$lock = Get-Content "$PSScriptRoot/windows-toolchain.json" -Raw | ConvertFrom-Json
$destination = Join-Path $ToolsRoot "innosetup-$($lock.innoVersion)-x64"
$compiler = Join-Path $destination 'ISCC.exe'
if (!(Test-Path $compiler)) {
    New-Item -ItemType Directory -Force $ToolsRoot | Out-Null
    $installer = Join-Path $ToolsRoot "innosetup-$($lock.innoVersion)-x64.exe"
    $tag = 'is-' + $lock.innoVersion.Replace('.', '_')
    Invoke-WebRequest "https://github.com/jrsoftware/issrc/releases/download/$tag/innosetup-$($lock.innoVersion)-x64.exe" -OutFile $installer
    if ((Get-FileHash $installer -Algorithm SHA256).Hash -ne $lock.innoSha256) {
        throw 'Inno Setup download failed SHA-256 verification.'
    }
    $signature = Get-AuthenticodeSignature $installer
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Pyrsys B\.V\.') {
        throw 'Inno Setup publisher signature is invalid.'
    }
    $process = Start-Process $installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
        '/CURRENTUSER', "/DIR=`"$destination`"") -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "Inno Setup installation failed: $($process.ExitCode)" }
}
if (!(Test-Path $compiler)) { throw 'Missing native x64 Inno Setup compiler.' }
& $compiler --version
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compiler did not start.' }
"INNO_ISCC=$compiler" | Add-Content $env:GITHUB_ENV
