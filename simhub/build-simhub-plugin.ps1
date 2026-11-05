# Builds LmuQuadView.SimHub.dll against your local SimHub installation.
# Usage (PowerShell, SimHub closed):
#   powershell -ExecutionPolicy Bypass -File build-simhub-plugin.ps1 [-SimHubDir "C:\Program Files (x86)\SimHub"] [-Install]
param([string]$SimHubDir = '', [switch]$Install)
$ErrorActionPreference = 'Stop'
if (!$SimHubDir) {
    $candidates = @("${env:ProgramFiles(x86)}\SimHub", "$env:ProgramFiles\SimHub")
    $SimHubDir = $candidates | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'SimHub.Plugins.dll') } | Select-Object -First 1
}
if (!$SimHubDir -or !(Test-Path -LiteralPath (Join-Path $SimHubDir 'SimHub.Plugins.dll'))) {
    throw 'SimHub.Plugins.dll not found. Pass -SimHubDir with your SimHub installation folder.'
}
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework 4.x C# compiler not found.' }
$output = Join-Path $PSScriptRoot 'build\LmuQuadView.SimHub.dll'
New-Item -ItemType Directory -Path (Split-Path $output) -Force | Out-Null
$references = @('SimHub.Plugins.dll', 'GameReaderCommon.dll') | ForEach-Object { '/reference:' + (Join-Path $SimHubDir $_) }
& $compiler /nologo /target:library /optimize+ /platform:anycpu "/lib:$SimHubDir" "/out:$output" @references `
    (Join-Path $PSScriptRoot 'QuadViewTelemetry.cs') (Join-Path $PSScriptRoot 'QuadViewSimHubPlugin.cs')
if ($LASTEXITCODE -ne 0) { throw 'Plugin compilation failed. If csc reports a missing referenced assembly (CS0012), add that SimHub DLL to $references.' }
Write-Output "Built $output"
if ($Install) {
    if (Get-Process -Name 'SimHubWPF' -ErrorAction SilentlyContinue) { throw 'Close SimHub before installing the plugin.' }
    Copy-Item -LiteralPath $output -Destination (Join-Path $SimHubDir 'LmuQuadView.SimHub.dll') -Force
    Write-Output "Installed into $SimHubDir. Start SimHub and enable 'LMU Quad View' when SimHub asks about the new plugin."
}
