param(
    [string]$LayerOutput = (Join-Path $PSScriptRoot 'build/lmu_quadview.dll'),
    [string]$AnchorImage
)
$ErrorActionPreference = 'Stop'
$LayerOutput = [IO.Path]::GetFullPath($LayerOutput)
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path 'build' -Force | Out-Null
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static tests/math_test.cpp -o build/math_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Math test compilation failed' }
    & .\build\math_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Math tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static -I ../prototype/third_party tests/focus_box_test.cpp -o build/focus_box_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Focus box tests compilation failed' }
    & .\build\focus_box_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Focus box tests failed' }
    foreach ($part in @('buffer','hook','trampoline')) {
        & gcc -O2 -c "vendor/minhook-1.3.4/src/$part.c" -o "build/mh_$part.o"
        if ($LASTEXITCODE -ne 0) { throw "MinHook $part compilation failed" }
    }
    & gcc -O2 -c vendor/minhook-1.3.4/src/hde/hde64.c -o build/mh_hde64.o
    if ($LASTEXITCODE -ne 0) { throw 'MinHook decoder compilation failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static -I ../prototype/third_party tests/engine_hooks_test.cpp nv_vrs.cpp build/mh_buffer.o build/mh_hook.o build/mh_trampoline.o build/mh_hde64.o -ld3d11 -ladvapi32 -o build/engine_hooks_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Engine hook tests compilation failed' }
    & .\build\engine_hooks_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Engine hook tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static -I ../prototype/third_party tests/native_outer_fallback_test.cpp nv_vrs.cpp build/mh_buffer.o build/mh_hook.o build/mh_trampoline.o build/mh_hde64.o -ld3d11 -ladvapi32 -o build/native_outer_fallback_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native fallback tests compilation failed' }
    & .\build\native_outer_fallback_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native fallback tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static tests/gpu_timing_test.cpp -ld3d11 -o build/gpu_timing_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'GPU timing tests compilation failed' }
    & .\build\gpu_timing_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'GPU timing tests failed' }
    foreach ($test in @('pipeline_stats','benchmark_control')) {
        & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static "tests/${test}_test.cpp" -ld3d11 -o "build/${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test tests compilation failed" }
        & ".\build\${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test tests failed" }
    }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static -I ../prototype/third_party tests/benchmark_engine_test.cpp nv_vrs.cpp build/mh_buffer.o build/mh_hook.o build/mh_trampoline.o build/mh_hde64.o -ld3d11 -ladvapi32 -o build/benchmark_engine_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Benchmark engine tests compilation failed' }
    & .\build\benchmark_engine_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Benchmark engine tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static tests/vrs_scope_policy_test.cpp -o build/vrs_scope_policy_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'VRS scope policy compilation failed' }
    if ($AnchorImage) { & .\build\vrs_scope_policy_test.exe $AnchorImage }
    else { & .\build\vrs_scope_policy_test.exe --policy-only }
    if ($LASTEXITCODE -ne 0) { throw 'VRS scope policy tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static tests/nv_vrs_test.cpp nv_vrs.cpp -ld3d11 -o build/nv_vrs_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'NV VRS helper tests compilation failed' }
    & .\build\nv_vrs_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'NV VRS helper tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static tests/ui_d3d_state_test.cpp -ld3d11 -ld3dcompiler -o build/ui_d3d_state_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'D3D11 state tests compilation failed' }
    & .\build\ui_d3d_state_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'D3D11 state tests failed' }
    foreach ($test in @('focus_overlay','outer_resample','native_outer_texture','native_outer_bindings')) {
        & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static "tests/${test}_test.cpp" -ld3d11 -ld3dcompiler -o "build/${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test tests compilation failed" }
        & ".\build\${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test tests failed" }
    }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static -I ../prototype/third_party xr_layer.cpp xr_layer_tests.cpp -ld3d11 -ld3dcompiler -o build/xr_layer_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'XR tests compilation failed' }
    foreach ($testArgs in @(@(),@('--srgb'),@('--stats'),@('--srgb','--stats'),@('--scene-stats'),@('--srgb','--scene-stats'),@('--capture'),@('--srgb','--capture'),@('--capture-no-trigger'),@('--capture-invalid'),@('--debug-focus'),@('--srgb','--debug-focus'),@('--scaled'),@('--srgb','--scaled'),@('--scaled','--debug-focus'),@('--srgb','--scaled','--debug-focus'),@('--scaled','--capture'),@('--invalid-sizes'),@('--focus-box'),@('--focus-box-large'),@('--invalid-focus-box'),@('--focus-box','--debug-focus'),@('--focus-box-large','--debug-focus'),@('--focus-box','--scaled','--srgb','--debug-focus'),@('--focus-asymmetric'),@('--focus-asymmetric','--debug-focus'),@('--focus-asymmetric','--scaled','--srgb','--debug-focus'),@('--runtime-settings'),@('--runtime-settings','--srgb'),@('--runtime-settings','--debug-focus'),@('--runtime-settings','--srgb','--debug-focus'),@('--runtime-equal'),@('--runtime-equal','--debug-focus'),@('--runtime-axis-equal'),@('--runtime-axis-equal','--debug-focus'),@('--runtime-too-wide'),@('--runtime-too-tall'),@('--runtime-overlay'),@('--runtime-overlay','--srgb'),@('--runtime-overlay','--debug-focus'),@('--runtime-overlay','--srgb','--debug-focus'))) {
        & .\build\xr_layer_tests.exe @testArgs
        if ($LASTEXITCODE -ne 0) { throw "XR tests failed: $testArgs" }
    }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static -shared -I ../prototype/third_party xr_layer.cpp engine_bridge.cpp nv_vrs.cpp build/mh_buffer.o build/mh_hook.o build/mh_trampoline.o build/mh_hde64.o -ld3d11 -ld3dcompiler -ladvapi32 -o $LayerOutput
    if ($LASTEXITCODE -ne 0) { throw 'Layer compilation failed' }
    Get-FileHash -LiteralPath $LayerOutput -Algorithm SHA256 | Select-Object Hash
} finally { Pop-Location }
