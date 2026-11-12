# Compact Notifications

Windhawk mod that replaces Windows toast banners with compact chat-style cards. Published in the Windhawk store
(`compact-notifications`); the user-facing description is the readme block inside the mod file.

- `compact-notifications.wh.cpp`: the mod (Windhawk mods are a single file).
- `docs/screenshot.png`: the screenshot shown in the store.
- `dev/` (local only, not in the repository): probe mod, test toasts, harness and install/log/screenshot scripts.

## How it works

- Hiding: a `SetWindowPos` hook in `ShellExperienceHost.exe` and `explorer.exe` moves the banner window off the
  screen, but only while the card window exists. The card window is created only after the reader thread has
  started, so if anything fails the normal banners show. The banner still exists, so clicks can be passed to it.
- Content: Windows shows one banner at a time and queues the rest (about 7 s each), but writes every toast to the
  notification database `%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db` at once. So cards come from
  there (one read-only connection through `winsqlite3.dll` from Windows; a folder change notification wakes the
  reader): texts, buttons, reply box and avatar from the toast XML, the app name from `shell:AppsFolder`. When the
  banner shows the toast later, it is matched to the card (by title and the start of the text) instead of making a
  second one. Toasts the database doesn't have (older Firefox versions) are read from the banner through UI
  Automation.
- Sound: `ShellExperienceHost` plays a toast's sound by opening its `.wav` from `%WINDIR%\Media` when the banner
  comes up, so with the queue the sounds came after the cards were gone. While the card window exists, a
  `CreateFileW` hook there fails those opens (except the looping `Alarm*`/`Ring*`), and the card thread plays one sound
  (`PlaySound` alias from `<audio src="ms-winsoundevent:…">`, else `Notification.Default`) per batch of new cards. It
  keeps Windows' choices: `<audio silent="true">`, `NOC_GLOBAL_SETTING_ALLOW_NOTIFICATION_SOUND`, the app's
  `SoundFile` (its "Play a sound" switch), and no sound for toasts older than 30 s. Muted apps get no card, so no sound.
- Drawing: a layered, never-activated window in the shell's `explorer.exe` (Direct2D/DirectWrite/WIC). Click and
  × go through UI Automation to the banner, so the app gets its real action.
- Replies are typed on the card itself (the card window takes the keyboard while its reply box is focused). The text
  goes into the toast's reply box (UI Automation `SetValue`) right before the button is pressed, on the banner or in
  the notification center.
- Actions on a card whose toast still waits in the queue (no banner yet): the mod opens the notification center
  with Win+N, kept off the screen (the card window's title tells the `ShellExperienceHost` hook to move it), finds
  the toast there, clicks the same element and closes the center again, then moves it back to where it was.
- × only closes the card; the toast stays in the notification center.

## Development

The tools live in `dev/` (not committed):

- `dev/install.ps1`: compiles the mod with Windhawk's compiler and flags, installs it as `local@compact-notifications`
  and lets Windhawk reload it.
- Log: create an empty `%TEMP%\compact-notifications.log`; the mod appends to it while the file exists.
- `dev/harness.ps1`: builds the reading and drawing part as a plain program with the log on the console.
- Syntax check only: `clang++ -std=c++23 -target x86_64-w64-mingw32 -include windhawk_api.h -DWH_MOD
  '-DWH_MOD_ID=L"compact-notifications"' '-DWH_MOD_VERSION=L"0.1"' -DUNICODE -D_UNICODE -DWINVER=0x0A00
  -D_WIN32_WINNT=0x0A00 -Wall -c -o /dev/null compact-notifications.wh.cpp` (Git Bash, compiler from
  `C:\Program Files\Windhawk\Compiler\bin`).

## Outside this folder

- Windhawk (`winget install RamenSoftware.Windhawk`).
- Windhawk keeps the source of local mods in `C:\ProgramData\Windhawk\ModsSource`, the compiled ones in
  `C:\ProgramData\Windhawk\Engine\Mods` and their settings in `HKLM\SOFTWARE\Windhawk\Engine\Mods`; `dev/install.ps1`
  writes there (`local@compact-notifications`), old DLLs are left behind.
- `%TEMP%\compact-notifications.log` (only if created) and `%TEMP%\toast-harness.exe` from the harness.

## License

GPL-3.0: the banner detection idea comes from the "Toast auto-dismiss" mod by nikitor32 and the banner window check
from "Notifications placement" by m417z (both GPL-3.0).
