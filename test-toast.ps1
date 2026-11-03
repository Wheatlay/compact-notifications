# Testowe powiadomienia Windows (Windows PowerShell 5.1, bez instalacji czegokolwiek).
# Użycie: powershell -File test-toast.ps1 [text|image|buttons|all]
param([string]$Kind = 'all')

[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] | Out-Null
[Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom.XmlDocument, ContentType = WindowsRuntime] | Out-Null

# Powiadomienia idą jako Windows PowerShell (ta aplikacja jest zarejestrowana w systemie).
$appId = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}/WindowsPowerShell/v1.0/powershell.exe'.Replace('/', [char]92)
$image = (Join-Path $env:WINDIR 'Web/Screen/img100.jpg')

$toasts = @{
	text    = '<toast><visual><binding template="ToastGeneric"><text>Ania Kowalska</text><text>Hej, widziałeś to? Krótka wiadomość testowa.</text></binding></visual></toast>'
	image   = "<toast><visual><binding template=`"ToastGeneric`"><image placement=`"appLogoOverride`" hint-crop=`"circle`" src=`"$image`"/><text>Bartek</text><text>Wiadomość z awatarem i dłuższym tekstem, żeby sprawdzić zawijanie w dwóch albo trzech liniach.</text></binding></visual></toast>"
	buttons = '<toast launch="test"><visual><binding template="ToastGeneric"><text>Spotkanie za 5 minut</text><text>Przycisk i pole odpowiedzi</text></binding></visual><actions><input id="reply" type="text" placeHolderContent="Odpowiedz"/><action content="Wyślij" arguments="send"/><action content="Odrzuć" arguments="dismiss"/></actions></toast>'
}

$kinds = if ($Kind -eq 'all') { 'text', 'image', 'buttons' } else { @($Kind) }
foreach ($k in $kinds) {
	$xml = New-Object Windows.Data.Xml.Dom.XmlDocument
	$xml.LoadXml($toasts[$k])
	[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($appId).Show([Windows.UI.Notifications.ToastNotification]::new($xml))
	Start-Sleep -Seconds 2
}
