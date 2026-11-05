# windhawk-toasts

Windhawk mod that replaces Windows toast banners with custom cards in a compact chat style.

- `compact-notifications.wh.cpp`: the mod.
- `probe.wh.cpp`: research mod, only logs the UI Automation tree of each toast banner.
- `test-toast.ps1`: shows test toasts (`text`, `image`, `buttons` or all).
- `dev/`: tools to work without the Windhawk editor (below).

## How it works

- Hiding: a `SetWindowPos` hook in `ShellExperienceHost.exe` and `explorer.exe` moves the banner window off the
  screen. The banner still exists, so clicks can be passed to it.
- Content: Windows shows one banner at a time and queues the rest (about 7 s each), but writes every toast to the
  notification database `%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db` at once. So cards come from
  there (read-only, through `winsqlite3.dll` from Windows; a folder change notification wakes the reader): texts,
  buttons, reply box and avatar from the toast XML, the app name from `shell:AppsFolder`. When the banner shows the
  toast later, it is matched to the card (by title and the start of the text) instead of making a second one.
  Toasts the database doesn't have (Firefox) are read from the banner through UI Automation, as before.
- Drawing: a layered, never-activated window in one `explorer.exe` process (Direct2D/DirectWrite/WIC). Click and
  × go through UI Automation to the banner, so the app gets its real action. Windhawk's separate "tool mod" process
  was tried first, but it didn't come back after a reload.
- Actions on a card whose toast still waits in the queue (no banner yet): the mod opens the notification center
  with Win+N, kept off the screen (the card window's title tells the `ShellExperienceHost` hook to move it), finds
  the toast there (unfolding the app's group, expanding the toast for its buttons and reply box), clicks or focuses
  the same element and closes the center again. Windows doesn't place the center again when it opens, so the mod
  moves it back to where it was.
- × only closes the card; the toast stays in the notification center. For a queued toast nothing is sent to Windows.
  (The banner's own close button, which the mod invokes for a banner toast, doesn't react to UI Automation anyway,
  tested on 05.11.2026.)

## Development without the editor

- `powershell -File dev/install.ps1`: compiles the mod with Windhawk's compiler and flags (same as the editor),
  copies it to `C:\ProgramData\Windhawk\Engine\Mods\64`, points `HKLM\SOFTWARE\Windhawk\Engine\Mods\local@compact-notifications`
  at it (logging on), and Windhawk reloads it. Elevates itself; with UAC prompts off this is silent.
- Log: create an empty `%TEMP%\compact-notifications.log`; the mod appends to it while the file exists. (Windhawk's own log
  goes through OutputDebugString, which `dev/log.ps1` collects, but nothing from this mod showed up there.)
- `powershell -File dev/harness.ps1`: builds the reading and drawing part as a plain program with the log on the
  console, outside Windhawk (the installed mod still hides the banner).
- `powershell -File dev/shot.ps1`: screenshot of the bottom right corner. Fails while the screen is locked or off.
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

GPL-3.0: the banner detection idea and the first tool-mod code come from the "Toast auto-dismiss" mod (GPL-3.0).
