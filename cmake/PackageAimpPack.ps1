param(
    [Parameter(Mandatory = $true)]
    [string] $StageDir,

    [Parameter(Mandatory = $true)]
    [string] $Dll64Path,

    [Parameter(Mandatory = $true)]
    [string] $Dll32Path,

    [Parameter(Mandatory = $true)]
    [string] $Host64Path,

    [Parameter(Mandatory = $true)]
    [string] $Host32Path,

    [Parameter(Mandatory = $true)]
    [string] $Scanner64Path,

    [Parameter(Mandatory = $true)]
    [string] $Scanner32Path,

    [Parameter(Mandatory = $true)]
    [string] $OutputFile
)

$ErrorActionPreference = 'Stop'

$pluginDir = Join-Path $StageDir 'dsp_vst3_bridge'
$x64Dir = Join-Path $pluginDir 'x64'
$x86Dir = Join-Path $pluginDir 'x86'
$binDir = Join-Path $pluginDir 'bin'
$outputDir = Split-Path -Parent $OutputFile
$zipFile = [System.IO.Path]::ChangeExtension($OutputFile, '.zip')

Remove-Item -LiteralPath $StageDir -Recurse -Force -ErrorAction SilentlyContinue
foreach ($required in @($Dll64Path, $Dll32Path, $Host64Path, $Host32Path, $Scanner64Path, $Scanner32Path)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required release binary is missing: $required" }
}
New-Item -ItemType Directory -Force -Path $x64Dir, $x86Dir, $binDir | Out-Null
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

Copy-Item -LiteralPath $Dll64Path -Destination (Join-Path $x64Dir 'dsp_vst3_bridge.dll') -Force
Copy-Item -LiteralPath $Dll32Path -Destination (Join-Path $x86Dir 'dsp_vst3_bridge.dll') -Force
Copy-Item -LiteralPath $Host64Path -Destination (Join-Path $binDir 'VST3BridgeHost64.exe') -Force
Copy-Item -LiteralPath $Host32Path -Destination (Join-Path $binDir 'VST3BridgeHost32.exe') -Force
Copy-Item -LiteralPath $Scanner64Path -Destination (Join-Path $binDir 'VST3BridgeScanner64.exe') -Force
Copy-Item -LiteralPath $Scanner32Path -Destination (Join-Path $binDir 'VST3BridgeScanner32.exe') -Force
@'
{
  "schemaVersion": 4,
  "storageMode": "automatic",
  "startupMode": "restoreLast",
  "scanOnStartup": true,
  "scanBridgeFolder": true,
  "scanSystemFolders": false
}
'@ | Set-Content -LiteralPath (Join-Path $pluginDir 'dsp_vst3_bridge_config.example.json') -Encoding utf8
Remove-Item -LiteralPath $zipFile -Force -ErrorAction SilentlyContinue

Compress-Archive -Path $pluginDir -DestinationPath $zipFile -CompressionLevel Optimal -Force
Move-Item -LiteralPath $zipFile -Destination $OutputFile -Force
