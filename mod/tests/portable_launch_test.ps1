$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../portable-launch.ps1')
function Assert($value,$message) { if (!$value) { throw $message } }
$root=Join-Path ([IO.Path]::GetTempPath()) ('qv-launch-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    $provider=Join-Path $root "provider's files"
    New-Item -ItemType Directory -Path $provider | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $provider 'layer.dll'),[byte[]](1,2,3))
    $manifest=Join-Path $provider 'quad.json'
    @{file_format_version='1.0.0';api_layer=@{name='XR_APILAYER_MBUCCHIA_quad_views_foveated';library_path='./layer.dll';api_version='1.0';implementation_version='1';disable_environment='DISABLE_QUAD_VIEWS_FOVEATED';enable_environment='ENABLE_TEST'}} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifest
    $before=Get-Content -LiteralPath $manifest -Raw
    $plan=New-LmuLayerPlan -Backend QVFR -ManifestRoot $root -QuadViewsManifest $manifest -RunDirectory (Join-Path $root 'run')
    Assert ($plan.Layers -eq 'XR_APILAYER_LMU_quadview;XR_APILAYER_MBUCCHIA_quad_views_foveated') 'Wrong Windows layer order/delimiter'
    Assert ($plan.DisableVariable -eq 'DISABLE_QUAD_VIEWS_FOVEATED') 'Missing process-local implicit disable'
    $copy=Get-Content -LiteralPath $plan.ExplicitManifest -Raw | ConvertFrom-Json
    Assert ([IO.Path]::IsPathFullyQualified($copy.api_layer.library_path)) 'Library must remain at original location'
    Assert (!$copy.api_layer.PSObject.Properties['disable_environment'] -and !$copy.api_layer.PSObject.Properties['enable_environment']) 'Explicit copy retained implicit activation switches'
    Assert ((Get-Content -LiteralPath $manifest -Raw) -ceq $before) 'Original manifest modified'
    Assert ($null -eq [Environment]::GetEnvironmentVariable('DISABLE_QUAD_VIEWS_FOVEATED','Process')) 'Host environment changed'
    $native=New-LmuLayerPlan -Backend Native -ManifestRoot $root
    Assert ($native.Layers -eq 'XR_APILAYER_LMU_quadview') 'Native backend changed'
    $bad=$before.Replace('layer.dll','missing.dll'); Set-Content -LiteralPath $manifest -Value $bad
    $failed=$false;try { New-LmuLayerPlan -Backend QVFR -ManifestRoot $root -QuadViewsManifest $manifest -RunDirectory (Join-Path $root 'bad') | Out-Null } catch { $failed=$true }
    Assert $failed 'Missing provider DLL accepted'
    $bad=$before.Replace('XR_APILAYER_MBUCCHIA_quad_views_foveated','unrelated_layer');Set-Content -LiteralPath $manifest -Value $bad
    $failed=$false;try { New-LmuLayerPlan -Backend QVFR -ManifestRoot $root -QuadViewsManifest $manifest -RunDirectory (Join-Path $root 'bad-name') | Out-Null } catch { $failed=$true }
    Assert $failed 'Wrong provider accepted'
    Write-Output 'Portable launch tests passed: layer order, manifest isolation, original library path, native defaults, missing/wrong provider'
} finally {
    if ([IO.Path]::GetFullPath($root).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $root -Recurse -Force }
}
