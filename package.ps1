param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist'),[string]$ArchiveName='', [switch]$RequireSigned)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'tools/version.ps1')
$version = Get-LmuVersion
if (!$ArchiveName) { $ArchiveName = 'LMU-Quad-View-Mod-' + $version + '.zip' }
# Refuse accidentally mixing binaries from an earlier release with current docs.
foreach ($binary in @('mod/build/lmu_quadview.dll','mod/build/lmu_autostart.dll','mod/build/LMU-QuadView-Launcher.exe')) {
    $metadata = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $PSScriptRoot $binary))
    if ($metadata.ProductVersion -cne $version -or $metadata.FileVersion -cne ($version + '.0')) {
        throw "$binary does not match VERSION ($version). Rebuild native code and the launcher before packaging."
    }
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if([IO.Path]::GetFileName($ArchiveName) -cne $ArchiveName -or !$ArchiveName.EndsWith('.zip',[StringComparison]::OrdinalIgnoreCase)){throw 'ArchiveName must be a ZIP filename, without directory components.'}
$archive = Join-Path $OutputDirectory $ArchiveName
if (Test-Path -LiteralPath $archive) { throw "Archive already exists: $archive" }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$stage = Join-Path $OutputDirectory ('package-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage,(Join-Path $stage 'build'),(Join-Path $stage 'licenses'),(Join-Path $stage 'tools'),(Join-Path $stage 'docs') | Out-Null
$files = @{
    'mod/build/lmu_quadview.dll' = 'build/lmu_quadview.dll'
    'mod/build/lmu_autostart.dll' = 'build/lmu_autostart.dll'
    'mod/build/LMU-QuadView-Launcher.exe' = 'LMU-QuadView-Launcher.exe'
    'mod/build/LMU-QuadView-Launcher.exe.config' = 'LMU-QuadView-Launcher.exe.config'
    'PORTABLE-QVFR.txt' = 'PORTABLE-QVFR.txt'
    'PORTABLE-VECTORXR.txt' = 'PORTABLE-VECTORXR.txt'
    'PORTABLE-VRS.txt' = 'PORTABLE-VRS.txt'
    'RENDER-PROFILING.txt' = 'RENDER-PROFILING.txt'
    'BENCHMARK.md' = 'BENCHMARK.md'
    'PORTABLE-STEREO-RESEARCH.txt' = 'PORTABLE-STEREO-RESEARCH.txt'
    'CPU-UPLOAD-REUSE.md' = 'CPU-UPLOAD-REUSE.md'
    'CPU-UPLOAD-FASTPATH.md' = 'CPU-UPLOAD-FASTPATH.md'
    'PERFORMANCE-EXPERIMENT.md' = 'PERFORMANCE-EXPERIMENT.md'
    'tools/analyze_stereo_audit.py' = 'tools/analyze_stereo_audit.py'
    'Start-SPS-CPU-Profile.cmd' = 'Start-SPS-CPU-Profile.cmd'
    'Start-SPS-Preparation.cmd' = 'Start-SPS-Preparation.cmd'
    'tools/start-sps-preparation.ps1' = 'tools/start-sps-preparation.ps1'
    'tools/start-sps-cpu-profile.ps1' = 'tools/start-sps-cpu-profile.ps1'
    'tools/analyze_sps_cpu_profile.py' = 'tools/analyze_sps_cpu_profile.py'
    'mod/launch.ps1' = 'launch.ps1'
    'mod/portable-launch.ps1' = 'portable-launch.ps1'
    'docs/SPS-CPU-PROFILING.md' = 'docs/SPS-CPU-PROFILING.md'
    'docs/SPS-CPU-PROFILE-RESULTS-20261104.md' = 'docs/SPS-CPU-PROFILE-RESULTS-20261104.md'
    'tools/start-frame-capture.ps1' = 'tools/start-frame-capture.ps1'
    'tools/start-frame-capture.cmd' = 'tools/start-frame-capture.cmd'
    'tools/check-hardware-stereo.cmd' = 'tools/check-hardware-stereo.cmd'
    'TODO.md' = 'TODO.md'
    'mod/XR_APILAYER_LMU_quadview.json' = 'XR_APILAYER_LMU_quadview.json'
    'README.md' = 'README.md'
    'README.txt' = 'README.txt'
    'VERSION' = 'VERSION'
    'CHANGELOG.md' = 'CHANGELOG.md'
    'CONTRIBUTING.md' = 'CONTRIBUTING.md'
    'docs/ARCHITECTURE.md' = 'docs/ARCHITECTURE.md'
    'docs/PERFORMANCE.md' = 'docs/PERFORMANCE.md'
    'docs/focus-validation.md' = 'docs/focus-validation.md'
    'docs/STEAM-LAUNCH.md' = 'docs/STEAM-LAUNCH.md'
    'docs/flicker-diagnosis-20261103.md' = 'docs/flicker-diagnosis-20261103.md'
    'docs/OPENGRIDXR-INTEGRATION.md' = 'docs/OPENGRIDXR-INTEGRATION.md'
    'docs/SPS-DEVELOPMENT.md' = 'docs/SPS-DEVELOPMENT.md'
    'docs/SPS-INTEGRATION.md' = 'docs/SPS-INTEGRATION.md'
    'docs/SPS-NATIVE-INTEGRATION.md' = 'docs/SPS-NATIVE-INTEGRATION.md'
    'docs/SPS-FOCUS-INPUTS.md' = 'docs/SPS-FOCUS-INPUTS.md'
    'docs/SPS-OWNER-INPUTS.md' = 'docs/SPS-OWNER-INPUTS.md'
    'docs/SPS-OWNER-BOUNDS.md' = 'docs/SPS-OWNER-BOUNDS.md'
    'PORTABLE-DESIGN.md' = 'PORTABLE-DESIGN.md'
    'STEREO-SHADER-CONVERSION.md' = 'STEREO-SHADER-CONVERSION.md'
    'INSTANCED-STEREO-HANDOFF.md' = 'INSTANCED-STEREO-HANDOFF.md'
    'LICENSE.md' = 'LICENSE.md'
    'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'mod/vendor/minhook-1.3.4/LICENSE.txt' = 'licenses/MinHook-LICENSE.txt'
    'prototype/third_party/openxr/LICENSE' = 'licenses/OpenXR-LICENSE.txt'
}
$proofPath='mod/build/msvc/stereo_instancing_test.exe'
if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot $proofPath))){$proofPath='mod/build/stereo_instancing_test.exe'}
$files[$proofPath]='tools/PortableStereoCheck.exe'
if($RequireSigned) {
    $publisher = $null
    foreach($source in $files.Keys | Where-Object { $_ -match '\.(dll|exe)$' }) {
        $signature=Get-AuthenticodeSignature -LiteralPath (Join-Path $PSScriptRoot $source)
        if($signature.Status -ne 'Valid' -or !$signature.TimeStamperCertificate) { throw "Valid timestamped signature required: $source" }
        if(!$publisher) { $publisher=$signature.SignerCertificate.Thumbprint }
        if($signature.SignerCertificate.Thumbprint -ne $publisher) { throw "Release binaries must have the same signer: $source" }
    }
}
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
