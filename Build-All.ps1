param(
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

$root = $PSScriptRoot
$x64 = Join-Path $root 'build_x64'
$x86 = Join-Path $root 'build_x86'

cmake -S $root -B $x64 -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE) { throw 'x64 CMake configuration failed' }
cmake --build $x64 --config $Configuration --target dsp_vst3_rack VST3RackHost VST3RackScanner BridgeRuntimeTests
if ($LASTEXITCODE) { throw 'x64 build failed' }
ctest --test-dir $x64 -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'x64 tests failed' }

cmake -S $root -B $x86 -G 'Visual Studio 17 2022' -A Win32
if ($LASTEXITCODE) { throw 'x86 CMake configuration failed' }
cmake --build $x86 --config $Configuration --target dsp_vst3_rack VST3RackHost VST3RackScanner BridgeRuntimeTests
if ($LASTEXITCODE) { throw 'x86 build failed' }
ctest --test-dir $x86 -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'x86 tests failed' }

$mtCommand = Get-Command mt.exe -ErrorAction SilentlyContinue
$mt = if ($mtCommand) { $mtCommand.Source } else {
    Get-ChildItem -LiteralPath "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter mt.exe |
        Where-Object { $_.Directory.Name -eq 'x64' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $mt) { throw 'Windows SDK manifest tool mt.exe was not found' }
foreach ($binary in @(
    (Join-Path $x64 "$Configuration\VST3RackHost64.exe"),
    (Join-Path $x86 "$Configuration\VST3RackHost32.exe"),
    (Join-Path $x64 "$Configuration\VST3RackScanner64.exe"),
    (Join-Path $x86 "$Configuration\VST3RackScanner32.exe")
)) {
    $manifest = [System.IO.Path]::GetTempFileName()
    & $mt -nologo "-inputresource:$binary;#1" "-out:$manifest"
    if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $manifest -Pattern 'PerMonitorV2' -Quiet)) {
        throw "Per-Monitor V2 manifest validation failed: $binary"
    }
    Remove-Item -LiteralPath $manifest -Force
}

& (Join-Path $root 'cmake\PackageAimpPack.ps1') `
    -StageDir (Join-Path $root 'build_package') `
    -Dll64Path (Join-Path $x64 "$Configuration\dsp_vst3_rack.dll") `
    -Dll32Path (Join-Path $x86 "$Configuration\dsp_vst3_rack.dll") `
    -Host64Path (Join-Path $x64 "$Configuration\VST3RackHost64.exe") `
    -Host32Path (Join-Path $x86 "$Configuration\VST3RackHost32.exe") `
    -Scanner64Path (Join-Path $x64 "$Configuration\VST3RackScanner64.exe") `
    -Scanner32Path (Join-Path $x86 "$Configuration\VST3RackScanner32.exe") `
    -OutputFile (Join-Path $root 'dist\dsp_vst3_rack.aimppack')

Write-Host "Package: $(Join-Path $root 'dist\dsp_vst3_rack.aimppack')"
