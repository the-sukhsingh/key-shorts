# KeyShorts

A tiny native Windows utility for global keyboard shortcuts, per-app shortcuts, key remaps, and key-sequence macros. Single small C++ executable, no installer, no runtime dependencies, starts with Windows, and lives in the tray.

Built with pure Win32 and a low-level keyboard hook so it stays out of the way: no Electron, no .NET, no background services.

## Features

- **Global shortcuts** that can launch a program, run a command, or type a key sequence.
- **Per-app scopes** - a rule can apply everywhere or only while a specific `exe` is focused.
- **Key remapping** - turn any key into any other key or key combination (hold-through, so modifiers work).
- **Macros** - send text and special keys with `{WAIT}`, `{CTRL DOWN}`, and similar tokens.
- **Repeat while held** for macro rules.
- **Master enable switch**, **run at startup**, and **tray** operation.
- **Human-editable config** at `%APPDATA%\KeyShorts\config.ini`.
- **Tiny footprint**: native Win32, single-digit MB private memory, no runtime.

## Build

Requirements: Visual Studio 2022 with the C++ tools (or CMake 3.20+ with any MSVC/MinGW toolchain).

```powershell
.\build.ps1
```

Output: `build\Release\KeyShorts.exe`.

Manual build:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Usage

1. Run `KeyShorts.exe`. It opens the rule manager and stays in the tray when closed.
2. Press **Add**, then **Set...** next to Trigger and press the key combination you want.
3. Choose an action:
   - **Launch program** - target is an exe or a URL (opened with `ShellExecute`).
   - **Run command** - target is passed to `cmd.exe /c`.
   - **Send keys** - a macro such as `Hello{ENTER}` or `{CTRL}c`.
   - **Remap key** - target is a key or combo such as `Esc` or `Ctrl+C`.
4. Pick a scope: **Global** or **Only in app** with an executable name like `chrome.exe`.
5. Changes save immediately and apply instantly.

Close the window to hide it to the tray. Right-click the tray icon for enable/disable, startup, config, and exit.

## Trigger and remap syntax

Triggers and remap targets use names joined by `+`:

```
Ctrl+Alt+T      Ctrl+Shift+Esc      Win+Space
CapsLock        F13                 LCtrl (modifier only)
Ctrl+Alt+Left   RCtrl+`            VK_A4
```

Modifier names: `Ctrl`, `Shift`, `Alt`, `Win`, plus left/right variants (`LCtrl`, `RCtrl`, `LShift`, `RShift`, `LAlt`, `RAlt`, `LWin`, `RWin`).

## Macro syntax

| Token | Meaning |
|---|---|
| plain text | typed as Unicode (layout-independent) |
| `{CTRL}`, `{SHIFT}`, `{ALT}`, `{WIN}` | apply to the next key |
| `{CTRL DOWN}` / `{CTRL UP}` | hold/release a modifier |
| `{ENTER}`, `{TAB}`, `{ESC}`, `{SPACE}`, `{BACKSPACE}` | named keys |
| `{F1}`..`{F24}`, `{UP}`, `{DOWN}`, `{PGUP}`, `{DELETE}` | named keys |
| `{WAIT 500}` | pause 500 ms |
| `{{` | literal `{` |

Example: `{CTRL}{SHIFT}{ESC}` opens Task Manager. `{WIN}r` opens Run. `Hi{ENTER}` types a message and sends it.

## Config

```ini
enabled=1
run_at_startup=1

[rule]
name=Open terminal
enabled=1
trigger=Ctrl+Alt+T
type=launch
target=wt.exe
args=
scope=global
app=
swallow=1
repeat=0
```

`type` is one of `launch`, `command`, `sendkeys`, `remap`. Scoped rules always win over global rules with the same trigger.

## Limits

- Windows hooks cannot see keys while an elevated (admin) window is focused unless KeyShorts also runs elevated.
- `Ctrl+Alt+Del`, `Win+L`, and other secure/reserved sequences cannot be intercepted.
- Some anti-cheat protected games block synthetic input.
- A low-level hook is powerful; some antivirus tools may warn about it.

## Memory

KeyShorts is a native Win32 app. Measure it yourself:

```powershell
.\tools\measure-memory.ps1
```
