param([string]$OutputPath = (Join-Path $PSScriptRoot 'build/LMU-QuadView-Launcher.exe'))
$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework x64 C# compiler not found' }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($OutputPath)) -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'build') -Force | Out-Null
$core = @((Join-Path $PSScriptRoot 'launcher/QvfrSettings.cs'),(Join-Path $PSScriptRoot 'launcher/LaunchService.cs'))
$references = @('/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/reference:System.Xml.Linq.dll','/reference:System.Web.Extensions.dll')
foreach ($test in @('SettingsTests','LaunchServiceTests')) {
    $testOutput = Join-Path $PSScriptRoot ('build/' + $test + '.exe')
    & $compiler /nologo /target:exe /platform:x64 /optimize+ /warnaserror+ "/main:$test" "/out:$testOutput" @references @core (Join-Path $PSScriptRoot "launcher/$test.cs")
    if ($LASTEXITCODE -ne 0) { throw "$test compilation failed" }
    & $testOutput
    if ($LASTEXITCODE -ne 0) { throw "$test failed" }
}
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /warnaserror+ "/out:$OutputPath" @references @core (Join-Path $PSScriptRoot 'launcher/Launcher.cs')
if ($LASTEXITCODE -ne 0) { throw 'Launcher compilation failed' }
Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256