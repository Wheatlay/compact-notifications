# windhawk-toasts

Windhawk mod that replaces Windows toast banners with custom ones in the style of Messy's toasts.

Status: research. `probe.wh.cpp` only logs the UI Automation tree of each toast banner.

## Plan

- Content (app, texts, images, actions) from the notification database
  `%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db` (SQLite, the toast XML is in `Notification.Payload`).
- Hiding the system banner: a hook inside `ShellExperienceHost.exe` (like the "Notifications placement" mod).
- Drawing, click and close in a separate windhawk.exe tool process. Click and close go through UI Automation on the
  hidden system banner, so the app gets its real action.

## Outside this folder

- Windhawk (`winget install RamenSoftware.Windhawk`).
- Windhawk keeps the source of local mods in `C:\ProgramData\Windhawk\ModsSource` and the compiled ones in
  `C:\ProgramData\Windhawk\Engine\Mods`; the mod's log is in Windhawk (mod page → Log).

## License

GPL-3.0: the banner detection and tool-mod code come from the "Toast auto-dismiss" mod (GPL-3.0).
