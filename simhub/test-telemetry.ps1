# Standalone Windows tests; does not start LMU/SimHub or install the plugin.
# Requires .NET Framework 4.x and g++ on PATH, or cl in an MSVC developer shell.
$ErrorActionPreference = 'Stop'
$build = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework 4.x C# compiler not found.' }
$fixtureSource = Join-Path $PSScriptRoot 'tests\telemetry_fixture.cpp'
$fixtureExe = Join-Path $build 'telemetry_fixture.exe'
$fixtureData = Join-Path $build 'telemetry-fixture.bin'
if (Get-Command g++ -ErrorAction SilentlyContinue) {
    & g++ -std=c++17 $fixtureSource -o $fixtureExe -ladvapi32
} elseif (Get-Command cl -ErrorAction SilentlyContinue) {
    & cl /nologo /std:c++17 /EHsc "/Fo$(Join-Path $build 'telemetry_fixture.obj')" "/Fe$fixtureExe" $fixtureSource /link advapi32.lib
} else { throw 'Run from an MSVC developer shell or add g++ to PATH.' }
if ($LASTEXITCODE -ne 0) { throw 'Native telemetry fixture compilation failed.' }
& $fixtureExe $fixtureData
if ($LASTEXITCODE -ne 0) { throw 'Native telemetry fixture generation failed.' }
$source = Join-Path $PSScriptRoot 'QuadViewTelemetry.cs'
$commands = Join-Path $PSScriptRoot '..\mod\launcher\ExternalCommands.cs'
foreach ($test in @('TelemetryTests', 'CommandTests')) {
    $output = Join-Path $build ($test + '.exe')
    & $compiler /nologo /target:exe /platform:x64 /optimize+ /warnaserror+ "/out:$output" $source $commands (Join-Path $PSScriptRoot "tests\$test.cs")
    if ($LASTEXITCODE -ne 0) { throw "$test compilation failed." }
    if ($test -eq 'TelemetryTests') { & $output $fixtureData } else { & $output }
    if ($LASTEXITCODE -ne 0) { throw "$test failed." }
}
