# Testowe powiadomienia Windows (Windows PowerShell 5.1, bez instalacji czegokolwiek).
# Użycie: powershell -File test-toast.ps1 [text|image|long|web|reminder|alarm|call|buttons|all]
param([string]$Kind = 'all')

[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] | Out-Null
[Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom.XmlDocument, ContentType = WindowsRuntime] | Out-Null

# Powiadomienia idą jako Windows PowerShell (ta aplikacja jest zarejestrowana w systemie).
$appId = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}/WindowsPowerShell/v1.0/powershell.exe'.Replace('/', [char]92)
$image = (Join-Path $env:WINDIR 'Web/Screen/img100.jpg')

$toasts = @{
	text    = '<toast><visual><binding template="ToastGeneric"><text>Ania Kowalska</text><text>Hej, widziałeś to? Krótka wiadomość testowa.</text></binding></visual></toast>'
	image   = "<toast><visual><binding template=`"ToastGeneric`"><image placement=`"appLogoOverride`" hint-crop=`"circle`" src=`"$image`"/><text>Bartek</text><text>Wiadomość z awatarem i dłuższym tekstem, żeby sprawdzić zawijanie w dwóch albo trzech liniach.</text></binding></visual></toast>"
	long    = "<toast><visual><binding template=`"ToastGeneric`"><image placement=`"appLogoOverride`" hint-crop=`"circle`" src=`"$image`"/><text>Ola</text><text>Bardzo długa wiadomość, żeby sprawdzić, jak karta rośnie w pionie. Ma kilka zdań, więc na pewno nie zmieści się w dwóch liniach, a może nawet w czterech. Jeśli jest dłuższa, reszta powinna się uciąć wielokropkiem na końcu ostatniej linii.</text></binding></visual></toast>"
	web     = '<toast><visual><binding template="ToastGeneric"><image placement="appLogoOverride" hint-crop="circle" src="https://avatars.githubusercontent.com/u/80686272?s=96"/><text>Wheatlay</text><text>Awatar pobrany z internetu</text></binding></visual></toast>'
	reminder ='<toast scenario="reminder"><visual><binding template="ToastGeneric"><text>Przypomnienie</text><text>Wyjście o 15:00</text></binding></visual><actions><action content="Odłóż" arguments="snooze" activationType="system"/><action content="Odrzuć" arguments="dismiss" activationType="system"/></actions></toast>'
	alarm   = '<toast scenario="alarm"><visual><binding template="ToastGeneric"><text>Budzik</text><text>7:00</text></binding></visual><actions><action content="Odłóż" arguments="snooze" activationType="system"/><action content="Odrzuć" arguments="dismiss" activationType="system"/></actions></toast>'
	call    = '<toast scenario="incomingCall"><visual><binding template="ToastGeneric"><text>Połączenie przychodzące</text><text>Kasia</text></binding></visual><actions><action content="Odbierz" arguments="answer"/><action content="Odrzuć" arguments="decline"/></actions></toast>'
	buttons ='<toast launch="test"><visual><binding template="ToastGeneric"><text>Spotkanie za 5 minut</text><text>Przycisk i pole odpowiedzi</text></binding></visual><actions><input id="reply" type="text" placeHolderContent="Odpowiedz"/><action content="Wyślij" arguments="send"/><action content="Odrzuć" arguments="dismiss"/></actions></toast>'
}

$kinds = if ($Kind -eq 'all') { 'text', 'image', 'long', 'buttons' } else { @($Kind) }
foreach ($k in $kinds) {
	$xml = New-Object Windows.Data.Xml.Dom.XmlDocument
	$xml.LoadXml($toasts[$k])
	[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($appId).Show([Windows.UI.Notifications.ToastNotification]::new($xml))
	if ($kinds.Count -gt 1) { Start-Sleep -Seconds 2 }
}
