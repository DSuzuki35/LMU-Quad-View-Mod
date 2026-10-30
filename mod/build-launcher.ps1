param([string]$OutputPath = (Join-Path $PSScriptRoot 'build/LMU-QuadView-Launcher.exe'))
$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework x64 C# compiler not found' }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($OutputPath)) -Force | Out-Null
$testOutput=Join-Path $PSScriptRoot 'build/launcher-tests.exe'
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($testOutput)) -Force | Out-Null
& $compiler /nologo /target:exe /main:LaunchCommandTests /platform:x64 /optimize+ /warnaserror+ "/out:$testOutput" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.Xml.Linq.dll (Join-Path $PSScriptRoot 'launcher/Launcher.cs') (Join-Path $PSScriptRoot 'launcher/LaunchCommandTests.cs')
if ($LASTEXITCODE -ne 0) { throw 'Launcher tests compilation failed' }
& $testOutput
if ($LASTEXITCODE -ne 0) { throw 'Launcher tests failed' }
& (Join-Path $PSScriptRoot 'tests/portable_launch_test.ps1')
& (Join-Path $PSScriptRoot 'tests/qvfr-profile-test.ps1')
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /warnaserror+ "/out:$OutputPath" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.Xml.Linq.dll (Join-Path $PSScriptRoot 'launcher/Launcher.cs')
if ($LASTEXITCODE -ne 0) { throw 'Launcher compilation failed' }
Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256
