# windhawk-toasts

Windhawk mod that replaces Windows toast banners with custom cards in a compact chat style.

- `compact-notifications.wh.cpp`: the mod.
- `probe.wh.cpp`: research mod, only logs the UI Automation tree of each toast banner.
- `test-toast.ps1`: shows test toasts (`text`, `image`, `buttons` or all).
- `dev/`: tools to work without the Windhawk editor (below).

## How it works

- Hiding: a `SetWindowPos` hook in `ShellExperienceHost.exe` and `explorer.exe` moves the banner window off the
  screen. The banner still exists, so clicks can be passed to it.
- Content: title, text and app name through UI Automation on the hidden banner; the avatar (`appLogoOverride`, local
  files only) from the notification database `%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db`
  (read-only, through `winsqlite3.dll` from Windows).
- Drawing: a layered, never-activated window in one `explorer.exe` process (Direct2D/DirectWrite/WIC). Click and
  × go through UI Automation to the banner, so the app gets its real action. Windhawk's separate "tool mod" process
  was tried first, but it didn't come back after a reload.

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
