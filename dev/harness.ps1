# Builds dev/harness.cpp around the mod and runs it for some seconds (log on the console).
# Usage: powershell -File dev/harness.ps1 [-Seconds 30] [-Source messy-toasts.wh.cpp]
param([int]$Seconds = 30, [string]$Source = 'messy-toasts.wh.cpp')

$ErrorActionPreference = 'Stop'
$wh = 'C:/Program Files/Windhawk'
$Source = (Resolve-Path $Source).Path -replace '\\', '/'
$text = Get-Content $Source -Raw
$options = [regex]::Match($text, '(?m)^// @compilerOptions\s+(.+)$').Groups[1].Value.Trim() -split '\s+' | Where-Object { $_ }
$exe = Join-Path $env:TEMP 'toast-harness.exe'
Get-Process toast-harness -ErrorAction SilentlyContinue | Stop-Process -Force

$flags = @('-std=c++23', '-O1', '-g', '-municode', '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
	'-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008', '-D__USE_MINGW_ANSI_STDIO=0',
	('-DMOD_SOURCE=\"' + $Source + '\"'), '-target', 'x86_64-w64-mingw32', '-static',
	(Join-Path $PSScriptRoot 'harness.cpp'), '-o', $exe, '-luser32') + $options
& "$wh/Compiler/bin/clang++.exe" @flags
if ($LASTEXITCODE -ne 0) { Write-Error "Compilation failed ($LASTEXITCODE)" }
& $exe $Seconds
Write-Output "exit $LASTEXITCODE"
