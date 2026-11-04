param([switch]$PrepareOnly)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$settingsPath=Join-Path $root 'launcher-settings.xml'
if(!(Test-Path -LiteralPath $settingsPath)){throw 'Open the launcher once and save your working game/provider settings first.'}
[xml]$settings=Get-Content -LiteralPath $settingsPath -Raw -Encoding UTF8
$config=$settings.QuadView
if([string]$config.UseRuntimeSettings -ne 'true'){throw 'This diagnostic entry point requires your existing runtime-controlled settings.'}
$arguments=@{
    Mode='quad';SpsCpuProfiling=$true;UseRuntimeSettings=$true
    Backend=[string]$config.Backend;GameDirectory=[string]$config.GameDirectory
    FoveationMode=[string]$config.FoveationMode
    NativeOuterResolution=([string]$config.NativeOuterResolution -eq 'true')
    FocusOffsetXPercent=[double]::Parse([string]$config.FocusOffsetXPercent,[Globalization.CultureInfo]::InvariantCulture)
    FocusOffsetYPercent=[double]::Parse([string]$config.FocusOffsetYPercent,[Globalization.CultureInfo]::InvariantCulture)
}
if($arguments.Backend -eq 'VectorXR' -and [string]$config.VectorXrManifest){$arguments.QuadViewsManifest=[string]$config.VectorXrManifest}
if($arguments.Backend -eq 'QVFR' -and [string]$config.QuadViewsManifest){$arguments.QuadViewsManifest=[string]$config.QuadViewsManifest}
if($PrepareOnly){[pscustomobject]$arguments;return}
Write-Output 'SPS CPU cost profile: press Ctrl+Alt+F10 on track. Capture saves after 120 seconds, or press again to stop.'
Write-Output 'Uses your saved provider and native outer target setting. Upload experiments, other profilers and heavy audit are disabled for this baseline.'
& (Join-Path $root 'launch.ps1') @arguments
