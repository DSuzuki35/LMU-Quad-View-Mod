param([switch]$PrepareOnly)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$settingsPath=Join-Path $root 'launcher-settings.xml'
if(!(Test-Path -LiteralPath $settingsPath)){throw 'Open the launcher once and save your working game/provider settings first.'}
[xml]$settings=Get-Content -LiteralPath $settingsPath -Raw -Encoding UTF8
$config=$settings.QuadView
$arguments=@{
    Mode='quad';SpsRendering=$true
    UseRuntimeSettings=([string]$config.UseRuntimeSettings -eq 'true')
    Backend=[string]$config.Backend;GameDirectory=[string]$config.GameDirectory
    FoveationMode=[string]$config.FoveationMode
    NativeOuterResolution=([string]$config.NativeOuterResolution -eq 'true')
    FocusOffsetXPercent=[double]::Parse([string]$config.FocusOffsetXPercent,[Globalization.CultureInfo]::InvariantCulture)
    FocusOffsetYPercent=[double]::Parse([string]$config.FocusOffsetYPercent,[Globalization.CultureInfo]::InvariantCulture)
}
if(!$arguments.UseRuntimeSettings){
    $arguments.OuterPixelsPercent=[double]::Parse([string]$config.OuterPercent,[Globalization.CultureInfo]::InvariantCulture)
    $arguments.FocusPixelsPercent=[double]::Parse([string]$config.InnerPercent,[Globalization.CultureInfo]::InvariantCulture)
    $arguments.FocusWidthPercent=[double]::Parse([string]$config.FocusWidthPercent,[Globalization.CultureInfo]::InvariantCulture)
    $arguments.FocusHeightPercent=[double]::Parse([string]$config.FocusHeightPercent,[Globalization.CultureInfo]::InvariantCulture)
}
if($arguments.Backend -eq 'VectorXR' -and [string]$config.VectorXrManifest){$arguments.QuadViewsManifest=[string]$config.VectorXrManifest}
if($arguments.Backend -eq 'QVFR' -and [string]$config.QuadViewsManifest){$arguments.QuadViewsManifest=[string]$config.QuadViewsManifest}
if($PrepareOnly){[pscustomobject]$arguments;return}
Write-Output 'Starting SPS rendering.'
& (Join-Path $root 'launch.ps1') @arguments
