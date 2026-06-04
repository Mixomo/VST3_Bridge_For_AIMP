param(
    [Parameter(Mandatory = $true)]
    [string] $StageDir,

    [Parameter(Mandatory = $true)]
    [string] $DllPath,

    [Parameter(Mandatory = $true)]
    [string] $HostPath,

    [Parameter(Mandatory = $true)]
    [string] $OutputFile
)

$ErrorActionPreference = 'Stop'

$pluginDir = Join-Path $StageDir 'dsp_vst3_bridge'
$x64Dir = Join-Path $pluginDir 'x64'
$outputDir = Split-Path -Parent $OutputFile
$zipFile = [System.IO.Path]::ChangeExtension($OutputFile, '.zip')

Remove-Item -LiteralPath $StageDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $x64Dir | Out-Null
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

Copy-Item -LiteralPath $DllPath -Destination (Join-Path $x64Dir 'dsp_vst3_bridge.dll') -Force
Copy-Item -LiteralPath $HostPath -Destination (Join-Path $x64Dir 'VST3BridgeHost.exe') -Force
Remove-Item -LiteralPath $zipFile -Force -ErrorAction SilentlyContinue

Compress-Archive -Path $pluginDir -DestinationPath $zipFile -CompressionLevel Optimal -Force
Move-Item -LiteralPath $zipFile -Destination $OutputFile -Force
