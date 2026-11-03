# Screenshot of the bottom right corner of the primary screen (where toasts appear), in physical pixels.
# Usage: powershell -File dev/shot.ps1 [-Out shot.png] [-Width 520] [-Height 700]
param([string]$Out = (Join-Path $env:TEMP 'toast-shot.png'), [int]$Width = 520, [int]$Height = 700)

Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type -Name Dpi -Namespace Native -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
[Native.Dpi]::SetProcessDPIAware() | Out-Null
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bitmap = New-Object System.Drawing.Bitmap $Width, $Height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($screen.Right - $Width, $screen.Bottom - $Height, 0, 0, $bitmap.Size)
$bitmap.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
Write-Output $Out
