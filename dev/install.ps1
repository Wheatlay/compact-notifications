# Compiles a mod like the Windhawk editor does and installs it as a local mod, without the editor.
# Usage: powershell -File dev/install.ps1 [messy-toasts.wh.cpp]
# Needs admin for Engine\Mods and HKLM (elevates itself; with UAC prompts off this is silent).
param([string]$Source = 'messy-toasts.wh.cpp', [string]$Dll, [string]$Id, [string]$Version)

$ErrorActionPreference = 'Stop'
$wh = 'C:/Program Files/Windhawk'
$data = 'C:/ProgramData/Windhawk'
$elevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
	[Security.Principal.WindowsBuiltInRole]::Administrator)

if ($Dll) {
	# Elevated part: copy the DLL and the source, point the mod's registry key at the new DLL.
	Copy-Item $Dll (Join-Path "$data/Engine/Mods/64" (Split-Path $Dll -Leaf))
	Copy-Item $Source (Join-Path "$data/ModsSource" "local@$Id.wh.cpp")
	$text = Get-Content $Source -Raw
	$include = ([regex]::Matches($text, '(?m)^// @include\s+(\S+)') | ForEach-Object { $_.Groups[1].Value }) -join '|'
	$key = "HKLM:/SOFTWARE/Windhawk/Engine/Mods/local@$Id"
	# New-Item -Force would recreate an existing key and wipe its values (settings included).
	if (-not (Test-Path $key)) { New-Item $key | Out-Null }
	Set-ItemProperty $key Version $Version
	Set-ItemProperty $key Include $include
	Set-ItemProperty $key Disabled 0 -Type DWord
	Set-ItemProperty $key LoggingEnabled 1 -Type DWord
	# Default settings, like the editor writes them on compile (only the ones not set yet). Groups become
	# "group.name" values; true/false and numbers are DWORDs, the rest strings.
	$settings = [regex]::Match($text, '(?s)// ==WindhawkModSettings==\s*/\*(.*?)\*/').Groups[1].Value
	$settingsKey = "$key/Settings"
	if (-not (Test-Path $settingsKey)) { New-Item $settingsKey | Out-Null }
	$group = ''
	foreach ($line in $settings -split "`r?`n") {
		if ($line -match '^- (\w+):\s*$') { $group = $Matches[1] + '.'; continue }
		if ($line -match '^- (\w+):\s*(.+)$') { $group = ''; $name = $Matches[1]; $value = $Matches[2] }
		elseif ($line -match '^  - (\w+):\s*(.+)$' -and $group) { $name = $group + $Matches[1]; $value = $Matches[2] }
		else { continue }
		if ($null -ne (Get-ItemProperty $settingsKey -Name $name -ErrorAction SilentlyContinue)) { continue }
		$value = $value.Trim().Trim('"')
		if ($value -eq 'true' -or $value -eq 'false') { Set-ItemProperty $settingsKey $name ([int]($value -eq 'true')) -Type DWord }
		elseif ($value -match '^-?\d+$') { Set-ItemProperty $settingsKey $name ([int]$value) -Type DWord }
		else { Set-ItemProperty $settingsKey $name $value -Type String }
	}
	# The engine reloads the mod when LibraryFileName changes, so that goes last.
	Set-ItemProperty $key SettingsChangeTime ([int][DateTimeOffset]::UtcNow.ToUnixTimeSeconds()) -Type DWord
	Set-ItemProperty $key LibraryFileName (Split-Path $Dll -Leaf)
	# Older builds of this mod; the one still loaded until the reload finishes stays and goes next time.
	Start-Sleep -Seconds 3
	Get-ChildItem "$data/Engine/Mods/64" -Filter "local@${Id}_*.dll" | Where-Object Name -ne (Split-Path $Dll -Leaf) |
		ForEach-Object { Remove-Item $_.FullName -ErrorAction SilentlyContinue }
	exit 0
}

$Source = (Resolve-Path $Source).Path
$text = Get-Content $Source -Raw
$Id = [regex]::Match($text, '(?m)^// @id\s+(\S+)').Groups[1].Value
$Version = [regex]::Match($text, '(?m)^// @version\s+(\S+)').Groups[1].Value
$options = [regex]::Match($text, '(?m)^// @compilerOptions\s+(.+)$').Groups[1].Value.Trim() -split '\s+' | Where-Object { $_ }
$engine = (Get-Content "$wh/windhawk.ini" | Select-String '^EnginePath=(.+)$').Matches[0].Groups[1].Value
$name = "local@${Id}_${Version}_$(Get-Random -Minimum 100000 -Maximum 999999).dll"
$out = Join-Path $env:TEMP $name

# Same flags as the editor (extension.js, compileModInternal).
$flags = @('-std=c++23', '-O2', '-shared', '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
	'-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008', '-D__USE_MINGW_ANSI_STDIO=0', '-DWH_MOD',
	# PowerShell 5.1 drops bare quotes inside native arguments, hence the backslashes.
	('-DWH_MOD_ID=L\"local@' + $Id + '\"'), ('-DWH_MOD_VERSION=L\"' + $Version + '\"'),"$wh/$engine/64/windhawk.lib",
	'-x', 'c++', $Source, '-include', 'windhawk_api.h', '-include', "$PSScriptRoot/devlog.h", '-target', 'x86_64-w64-mingw32',
	'-Wl,--export-all-symbols', '-o', $out) + $options
Push-Location "$wh/Compiler"
& "$wh/Compiler/bin/clang++.exe" @flags
$code = $LASTEXITCODE
Pop-Location
if ($code -ne 0) { Write-Error "Compilation failed ($code)" }

if ($elevated) {
	& $PSCommandPath -Source $Source -Dll $out -Id $Id -Version $Version
} else {
	$argLine = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Source `"$Source`" -Dll `"$out`" -Id $Id -Version $Version"
	$p = Start-Process powershell -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList $argLine
	if ($p.ExitCode -ne 0) { Write-Error "Install failed ($($p.ExitCode))" }
}
Remove-Item $out
Write-Output "Installed local@$Id as $name"

