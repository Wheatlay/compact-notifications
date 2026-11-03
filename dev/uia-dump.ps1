# Dumps the UI Automation tree of the toast banner (also when the mod has moved it off the screen).
# Usage: powershell -File dev/uia-dump.ps1
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$root = [Windows.Automation.AutomationElement]::RootElement
$condition = New-Object Windows.Automation.PropertyCondition ([Windows.Automation.AutomationElement]::ClassNameProperty), 'Windows.UI.Core.CoreWindow'
$walker = [Windows.Automation.TreeWalker]::RawViewWalker

function Dump($element, $depth) {
	$c = $element.Current
	$r = $c.BoundingRectangle
	'{0}{1} class="{2}" id="{3}" name="{4}" rect={5},{6} {7}x{8}' -f ('  ' * $depth), $c.ControlType.ProgrammaticName, $c.ClassName,
		$c.AutomationId, $c.Name, $r.X, $r.Y, $r.Width, $r.Height
	if ($depth -lt 8) {
		$child = $walker.GetFirstChild($element)
		while ($child) {
			Dump $child ($depth + 1)
			$child = $walker.GetNextSibling($child)
		}
	}
}

# The banner isn't listed among the desktop's UIA children (nor by EnumWindows), but FindWindowEx finds it.
Add-Type -Name Win -Namespace Dump -MemberDefinition '[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);'
$hwnd = [IntPtr]::Zero
while (($hwnd = [Dump.Win]::FindWindowEx([IntPtr]::Zero, $hwnd, 'Windows.UI.Core.CoreWindow', $null)) -ne [IntPtr]::Zero) {
	$window = [Windows.Automation.AutomationElement]::FromHandle($hwnd)
	if ($window.Current.Name -match 'powiadomienie|notification' -and $window.Current.BoundingRectangle.Height -gt 60) {
		Dump $window 0
	}
}
