#requires -Version 7.6.10
#requires -PSEdition Core
param(
    [Parameter(Mandatory=$true)][ValidateSet('Reference','Candidate')][string]$Role,
    [Parameter(Mandatory=$true)][ValidateSet('ZoneMainMenuNew','zone7')][string]$Scene,
    [Parameter(Mandatory=$true)][int]$GameProcessId,
    [Parameter(Mandatory=$true)][string]$RendererIni,
    [Parameter(Mandatory=$true)][string]$CameraAndWorldState,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
# Run manually only after the owner starts and positions the selected game.
# This script neither starts a process nor installs/configures the game.
$process = Get-Process -Id $GameProcessId
$expected = if ($Role -eq 'Reference') { 'game' } else { 'EIRebuild' }
if ($process.ProcessName -ne $expected -or $process.MainWindowHandle -eq 0) {
    throw "Expected a visible $expected process."
}
$ini = (Resolve-Path -LiteralPath $RendererIni).Path
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) { throw 'Choose a new capture directory.' }
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class RS02bCapture {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][RS02bCapture]::SetProcessDPIAware()
Write-Host 'Activate the game window now; capture starts in five seconds.'
Start-Sleep -Seconds 5
$window = $process.MainWindowHandle
if ([RS02bCapture]::GetForegroundWindow() -ne $window) { throw 'Selected game is not foreground.' }
$rect = New-Object RS02bCapture+Rect
$origin = New-Object RS02bCapture+Point
if (-not [RS02bCapture]::GetClientRect($window,[ref]$rect) -or
    -not [RS02bCapture]::ClientToScreen($window,[ref]$origin)) { throw 'Cannot locate game client area.' }
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) { throw 'Empty game client area.' }
New-Item -ItemType Directory -Path $destination | Out-Null
$bitmap = New-Object Drawing.Bitmap($width,$height)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.CopyFromScreen($origin.X,$origin.Y,0,0,$bitmap.Size)
    $bitmap.Save((Join-Path $destination 'frame.png'),[Drawing.Imaging.ImageFormat]::Png)
} finally { $graphics.Dispose(); $bitmap.Dispose() }
Copy-Item -LiteralPath $ini -Destination (Join-Path $destination 'renderer-settings.ini')
$receipt = [ordered]@{
    role=$Role; scene=$Scene; state=$CameraAndWorldState; utc=[DateTime]::UtcNow.ToString('o')
    width=$width; height=$height; process=$process.ProcessName
    executable=$process.Path; executable_sha256=(Get-FileHash -LiteralPath $process.Path).Hash
    settings_sha256=(Get-FileHash -LiteralPath (Join-Path $destination 'renderer-settings.ini')).Hash
    frame_sha256=(Get-FileHash -LiteralPath (Join-Path $destination 'frame.png')).Hash
    method='foreground client-area screen capture; visual inspection required'
    performance='not measured by this script'
}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'capture.json') -Encoding UTF8
Write-Host "Saved $destination. Check for overlays/occlusion and matching scene/camera/settings."
