param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist'),[string]$ArchiveName='LMU-Quad-View-Mod-portable-stereo-research2.zip')
$ErrorActionPreference = 'Stop'
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if([IO.Path]::GetFileName($ArchiveName) -cne $ArchiveName -or !$ArchiveName.EndsWith('.zip',[StringComparison]::OrdinalIgnoreCase)){throw 'ArchiveName must be a ZIP filename, without directory components.'}
$archive = Join-Path $OutputDirectory $ArchiveName
if (Test-Path -LiteralPath $archive) { throw "Archive already exists: $archive" }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$stage = Join-Path $OutputDirectory ('package-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage,(Join-Path $stage 'build'),(Join-Path $stage 'licenses'),(Join-Path $stage 'tools') | Out-Null
$files = @{
    'mod/build/lmu_quadview.dll' = 'build/lmu_quadview.dll'
    'mod/build/LMU-QuadView-Launcher.exe' = 'LMU-QuadView-Launcher.exe'
    'mod/build/LMU-QuadView-Launcher.exe.config' = 'LMU-QuadView-Launcher.exe.config'
    'PORTABLE-QVFR.txt' = 'PORTABLE-QVFR.txt'
    'PORTABLE-VECTORXR.txt' = 'PORTABLE-VECTORXR.txt'
    'RENDER-PROFILING.txt' = 'RENDER-PROFILING.txt'
    'PORTABLE-STEREO-RESEARCH.txt' = 'PORTABLE-STEREO-RESEARCH.txt'
    'tools/analyze_stereo_audit.py' = 'tools/analyze_stereo_audit.py'
    'tools/check-hardware-stereo.cmd' = 'tools/check-hardware-stereo.cmd'
    'TODO.md' = 'TODO.md'
    'mod/XR_APILAYER_LMU_quadview.json' = 'XR_APILAYER_LMU_quadview.json'
    'README.md' = 'README.md'
    'README.txt' = 'README.txt'
    'LICENSE.md' = 'LICENSE.md'
    'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'mod/vendor/minhook-1.3.4/LICENSE.txt' = 'licenses/MinHook-LICENSE.txt'
    'prototype/third_party/openxr/LICENSE' = 'licenses/OpenXR-LICENSE.txt'
}
$proofPath='mod/build/msvc/stereo_instancing_test.exe'
if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot $proofPath))){$proofPath='mod/build/stereo_instancing_test.exe'}
$files[$proofPath]='tools/PortableStereoCheck.exe'
foreach ($source in $files.Keys) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $source) -Destination (Join-Path $stage $files[$source])
}
$nvapi = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'mod/vendor/nvapi/nvapi.h')
$noticeEnd = 0
while ($noticeEnd -lt $nvapi.Count -and $nvapi[$noticeEnd] -notmatch '^\\\*+/') { $noticeEnd++ }
if ($noticeEnd -ge $nvapi.Count) { throw 'NVIDIA license block was not found' }
$nvapi[0..$noticeEnd] | Set-Content -LiteralPath (Join-Path $stage 'licenses/NVAPI-LICENSE.txt')
Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    $prefix=[IO.Path]::GetFullPath($stage).TrimEnd('\')+'\'
    if(!$_.FullName.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Package entry escaped staging directory.'}
    $relative = $_.FullName.Substring($prefix.Length).Replace('\','/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash,$relative
} | Set-Content -LiteralPath (Join-Path $stage 'SHA256.txt')
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive
Write-Output "Portable folder: $stage"
Write-Output "Portable archive: $archive"
