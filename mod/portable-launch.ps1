# Process-local layer selection. Never changes runtime registration or user settings.
function Find-LmuQuadViewsManifest {
    param([string]$Path)
    if ($Path) {
        if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Quad-Views-Foveated manifest not found: $Path" }
        return (Resolve-Path -LiteralPath $Path).Path
    }
    $candidates=[Collections.Generic.List[string]]::new()
    foreach ($hive in @('HKLM','HKCU')) {
        $key=Get-Item -LiteralPath "${hive}:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" -ErrorAction SilentlyContinue
        if ($key) { foreach ($name in $key.GetValueNames()) { if ($name.EndsWith('.json',[StringComparison]::OrdinalIgnoreCase)) { $candidates.Add($name) } } }
    }
    foreach ($directory in @('OpenXR-Quad-Views-Foveated','Quad-Views-Foveated')) {
        if ($env:ProgramFiles) {
            $folder=Join-Path $env:ProgramFiles $directory
            if (Test-Path -LiteralPath $folder -PathType Container) {
                foreach ($file in Get-ChildItem -LiteralPath $folder -Filter '*.json' -File) { $candidates.Add($file.FullName) }
            }
        }
    }
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (!(Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        try { $doc=Get-Content -LiteralPath $candidate -Raw | ConvertFrom-Json -ErrorAction Stop } catch { continue }
        if ($doc.api_layer.name -eq 'XR_APILAYER_MBUCCHIA_quad_views_foveated') { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw 'Quad-Views-Foveated was not found. Install it from the upstream project, or select its JSON manifest.'
}
function New-LmuLayerPlan {
    param([ValidateSet('Native','QVFR')][string]$Backend='Native',[Parameter(Mandatory)][string]$ManifestRoot,[string]$QuadViewsManifest,[string]$RunDirectory)
    $root=[IO.Path]::GetFullPath($ManifestRoot)
    if ($root.Contains(';')) { throw 'The package path cannot contain a semicolon (OpenXR search-path separator).' }
    if ($Backend -eq 'Native') {
        # Installed implicit QVFR would otherwise sit above our explicit adapter.
        # Selecting Native means use the runtime's implementation instead.
        $disabled=$null
        try {
            $installed=Find-LmuQuadViewsManifest
            $implicit=Get-Content -LiteralPath $installed -Raw | ConvertFrom-Json
            $candidate=[string]$implicit.api_layer.disable_environment
            if ($candidate -match '^[A-Za-z_][A-Za-z0-9_]*$' -and $candidate -match '(?i)quad') { $disabled=$candidate }
        } catch { } # Native operation does not require a QVFR installation.
        return [pscustomobject]@{Paths=$root;Layers='XR_APILAYER_LMU_quadview';DisableVariable=$disabled;ProviderManifest=$null;ExplicitManifest=$null;Library=$null}
    }
    $manifest=Find-LmuQuadViewsManifest -Path $QuadViewsManifest
    $doc=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json -AsHashtable
    $layer=$doc.api_layer
    if (!$layer -or $layer.name -ne 'XR_APILAYER_MBUCCHIA_quad_views_foveated') { throw 'Selected file is not a Quad-Views-Foveated layer manifest.' }
    $disable=[string]$layer.disable_environment
    if (!$disable -or $disable -notmatch '^[A-Za-z_][A-Za-z0-9_]*$' -or $disable -notmatch '(?i)quad') { throw 'The QVFR manifest has no recognizable disable_environment switch; cannot safely order the layers.' }
    $library=[string]$layer.library_path
    if (!$library) { throw 'The QVFR manifest has no library_path.' }
    if (![IO.Path]::IsPathFullyQualified($library)) { $library=Join-Path ([IO.Path]::GetDirectoryName($manifest)) $library }
    $library=[IO.Path]::GetFullPath($library)
    if (!(Test-Path -LiteralPath $library -PathType Leaf)) { throw "QVFR library not found: $library" }
    if (!$RunDirectory) { throw 'A unique run directory is required for QVFR.' }
    $run=[IO.Path]::GetFullPath($RunDirectory)
    if ($run.Contains(';')) { throw 'The run path cannot contain a semicolon.' }
    if (Test-Path -LiteralPath $run) { throw 'Layer run directory already exists; refusing to overwrite it.' }
    $layer.library_path=$library
    [void]$layer.Remove('disable_environment');[void]$layer.Remove('enable_environment')
    New-Item -ItemType Directory -Path $run -ErrorAction Stop | Out-Null
    $explicit=Join-Path $run 'XR_APILAYER_MBUCCHIA_quad_views_foveated.json'
    $doc | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $explicit -Encoding utf8
    [pscustomobject]@{Paths="$root;$run";Layers='XR_APILAYER_LMU_quadview;XR_APILAYER_MBUCCHIA_quad_views_foveated';DisableVariable=$disable;ProviderManifest=$manifest;ExplicitManifest=$explicit;Library=$library}
}
