# Changes one setting of an installed local mod, like the settings page in Windhawk (elevates itself).
# Usage: powershell -File dev/setting.ps1 position.corner topRight [-Id messy-toasts]
# The running mod gets it on the next load (Windhawk only notifies it when the change comes from its own UI).
param([Parameter(Mandatory)][string]$Name, [Parameter(Mandatory)][string]$Value, [string]$Id = 'messy-toasts')

$elevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
	[Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $elevated) {
	$p = Start-Process powershell -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList (
		"-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Name `"$Name`" -Value `"$Value`" -Id $Id")
	exit $p.ExitCode
}
$key = "HKLM:/SOFTWARE/Windhawk/Engine/Mods/local@$Id"
if ($Value -match '^(true|false)$') { Set-ItemProperty "$key/Settings" $Name ([int]($Value -eq 'true')) -Type DWord }
elseif ($Value -match '^-?\d+$') { Set-ItemProperty "$key/Settings" $Name ([int]$Value) -Type DWord }
else { Set-ItemProperty "$key/Settings" $Name $Value -Type String }
Set-ItemProperty $key SettingsChangeTime ([int][DateTimeOffset]::UtcNow.ToUnixTimeSeconds()) -Type DWord
