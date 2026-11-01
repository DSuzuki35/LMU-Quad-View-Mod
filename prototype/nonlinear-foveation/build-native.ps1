param([string]$Compiler = 'g++.exe')
$ErrorActionPreference = 'Stop'
$output = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../mod/build/nonlinear/native'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$executable = Join-Path $output 'nonlinear-native.exe'
& $Compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static (Join-Path $PSScriptRoot 'native.cpp') -ld3d11 -ld3dcompiler -ldxgi -ldxguid -o $executable
if ($LASTEXITCODE -ne 0) { throw 'Native nonlinear probe compilation failed' }
foreach ($mode in @('warp','hardware')) {
    $arguments = @((Join-Path $PSScriptRoot 'native.hlsl'),$output)
    if ($mode -eq 'hardware') { $arguments += '--hardware' }
    & $executable @arguments | Set-Content -LiteralPath (Join-Path $output ($mode+'-report.json')) -Encoding UTF8
    if ($LASTEXITCODE -ne 0) { throw "Native nonlinear $mode checks failed" }
    Write-Host "Native nonlinear $mode checks passed"
}
