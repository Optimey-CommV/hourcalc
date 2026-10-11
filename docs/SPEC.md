# HourCalc - specification

> **Note for the public repository.** This is a working document of the development. Material it
> mentions that was only working material is not published: the earlier WPF build (`legacy\wpf\`),
> its snapshots (`docs\snapshots\`), the snapshot renders and comparisons, the logo proposals
> (`logos\`) and a reference screenshot of Microsoft's Calculator.

HourCalc is a small, portable Windows tool that converts hours between
`h:mm` notation and decimal hours, and calculates with hours. It looks and
behaves like the Windows 11 Calculator.

Owner's request (paraphrased): "a converter that looks like the Windows
calculator. Left field takes hours like 08:30, the right field shows the
decimal value 8,50 automatically. If the left field is empty and I type 8,50
on the right, the left shows 08:30. A Reset button clears the fields and a
Convert button forces the conversion. Tab 2 is a calculator that works in
hours: 07:30 + 07:20 = 14 h 50 min in the left result field and the decimal
value in the right result field. Portable, as small as possible, with its own
logo as exe icon, window icon and taskbar icon."

All text that ends up on the machine is English: UI strings, code comments,
file contents, README. Numbers follow the Windows regional settings (the
owner's machine is nl-BE, decimal separator `,`).

> **2026-10-10: superseded for sections 1 and 2.** The WPF build started too slowly (1.4-2.5 s,
> measured). HourCalc is being ported to native C++ / Win32; `docs\NATIVE.md` defines the
> technology, structure and acceptance criteria. Sections 3-8 below remain the behaviour
> specification; the WPF build (`legacy\wpf\`, `docs\snapshots\`) is the reference.

## 1. Technology (decided, do not change)

- C# WPF on .NET Framework 4.8 (built into Windows 10/11). One `HourCalc.exe`,
  nothing else to ship. No installer, no registry writes (portable). The only
  file ever written is `HourCalc.ini`, in `%LOCALAPPDATA%\HourCalc\` (section 6c;
  first written next to the exe, changed on 2026-10-10), and only when the user
  changes a setting in the menu (sections 6 and 6b). If that file cannot be
  written, the setting silently applies to the current session only.
- Compiler: Roslyn from the installed .NET SDK, against the runtime assemblies
  of the installed framework (there is no targeting pack on this machine, and
  no NuGet downloads are allowed). Verified working command:

  ```
  $FW = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319"
  dotnet "C:\Program Files\dotnet\sdk\6.0.428\Roslyn\bincore\csc.dll" -nologo -noconfig -nostdlib `
    -langversion:7.3 -target:winexe -optimize+ -debug- -out:dist\HourCalc.exe `
    -r:$FW\mscorlib.dll -r:$FW\System.dll -r:$FW\System.Core.dll -r:$FW\System.Xaml.dll `
    -r:$FW\WPF\WindowsBase.dll -r:$FW\WPF\PresentationCore.dll -r:$FW\WPF\PresentationFramework.dll `
    -win32icon:assets\icon\app.ico -win32manifest:src\app.manifest src\*.cs
  ```

  `-langversion:7.3` is the ceiling (later C# features need runtime support
  .NET Framework lacks). Add `-r:` for `Microsoft.Win32.Registry` only if
  needed (on .NET Framework the registry lives in mscorlib, so it is not).
- No compiled XAML (no MSBuild markup compiler is available). Build the UI in
  C# code. Styles and control templates may be written as a XAML string that is
  parsed once at startup with `System.Windows.Markup.XamlReader.Parse`, if that
  keeps the code clearer; otherwise build templates in code.
- `src\app.manifest`: PerMonitorV2 DPI awareness (`<dpiAwareness>` plus
  `<dpiAware>true/pm</dpiAware>`), `supportedOS` for Windows 10/11,
  `asInvoker`.
- Assembly attributes: title/product `HourCalc`, version `1.0.0.0`, company
  `Optimey CommV`, copyright `Copyright (c) 2026 Optimey CommV`.
- Target size: under 200 KB in total. The program itself (code, icon, manifest) is about 125 KB;
  the rest is the GPL payload of section 8 (a slim source archive that also holds the license).

## 2. Code structure

- `src\TimeMath.cs` - pure logic, no WPF references: parsing, formatting,
  rounding. Fully unit-testable.
- `src\CalcEngine.cs` - pure calculator state machine (no WPF), driven by
  `Press(string key)` with keys `"0".."9"`, `":"`, `","`, `"+"`, `"-"`, `"*"`,
  `"/"`, `"="`, `"CE"`, `"C"`, `"BACK"`. Exposes the display state
  (expression line, main text, decimal text, caption, error flag).
- `src\Converter.cs` (optional) - pure converter field logic if useful.
- `src\Theme.cs` - colors, light/dark detection, accent palette, DWM caption.
- `src\Styles.cs` - the control styles and templates (one XAML string).
- `src\MainWindow.cs`, `src\App.cs` (entry point with `[STAThread] Main`).
- `src\Settings.cs` (HourCalc.ini, section 6), `src\Embedded.cs` (the embedded license and source
  archive, the legal notice), `src\Dialogs.cs` (About window and license viewer, section 8).
- Testability: the window class and its keypad handling must be drivable
  without real input, so a separate tool can render screenshots:
  public `MainWindow.Press(string key)` (routes to the active tab),
  public `MainWindow.SelectTab(int index)`, public
  `MainWindow.ApplyTheme(bool dark)` (force a theme), public
  `MainWindow.SetConverterText(int field, string text)`.
- `tests\Tests.cs` - a console test runner compiled together with
  `TimeMath.cs` and `CalcEngine.cs` (`-target:exe`), no test framework.
  Exit code 0 only if every test passes; print each failure. `build.ps1` also runs the off-screen
  UI tests (`tests\UiTests.cs`) and the adversarial review probes (`tests\ReviewTests.cs`), both
  against `dist\HourCalc.exe` (`MainWindow` takes a settings path; tests pass `null`: no ini file).
- `tools\Snap.cs` - a console/WPF tool that references `dist\HourCalc.exe` as
  an assembly, creates `MainWindow` off-screen, drives states and renders the
  window content with `RenderTargetBitmap` to PNG, for both themes. It must
  not show a visible window on the user's screen.
- `tools\make_ico.py` - builds a multi-size `.ico` from an SVG
  (`python tools\make_ico.py <icon.svg> [--small <icon-small.svg>]
  [--size N=<icon-N.svg> ...] <out.ico>`):
  sizes 16, 20, 24, 32, 40, 48, 64, 256; the 256 frame PNG-compressed, the
  others as 32-bit BMP/PNG frames; when `--small` is given, that SVG is used
  for 16-32 px; `--size N=<svg>` (repeatable) overrides the drawing of the
  single frame N, whatever `--small` says. Render with `resvg_py` (installed)
  and assemble with Pillow or by hand (verify the result loads in
  `System.Drawing.Icon` with every size).
- `build.ps1` - ASCII only (Windows PowerShell 5.1 reads non-BOM files as
  ANSI). Steps: compile app, compile and run tests (fail the build on a
  failing test), optional `-Sign` (Authenticode SHA256 with the certificate
  thumbprint `DC5A657A60F166815ACDA04E71581F7313553BEA` from
  `Cert:\CurrentUser\My`, timestamp `http://timestamp.digicert.com`; skip
  silently when the switch is absent), print the exe size.
- Write files with LF line endings, UTF-8 without BOM, except `build.ps1`
  which must be pure ASCII.

## 3. Window and look (Windows 11 Calculator)

Reference screenshot (light theme, 96 DPI, measured):
`docs\reference\wincalc-light.png` (322 x 547 window including border).

Measured from that screenshot:

| element | light value |
|---|---|
| window / title bar / header background | `#F0F4F7` |
| function buttons (CE, C, backspace, operators) | `#F7F9FB` |
| digit buttons | `#FFFFFF` |
| `=` button | accent dark 1 = `#0067C0` (from the registry, see below), white text |
| button border | 1 px, light top `~#E5E8EA`, darker bottom `~#D5D7DA` (WinUI elevation border) |
| button size / gap | ~75 x 52 px keys, measured gaps: 3 px between columns, 2 px between rows; 4 px to the left and bottom edge, 5 px to the right (layout rounding) |
| corner radius | 4 px |

Dark theme (not measured; the owner's machine is in light mode and system
settings must not be changed): background `#202020`, function buttons
`#323232`, digit buttons `#3B3B3B`, border `#3F3F3F`/`#2A2A2A`, text
`#FFFFFF`, secondary text `#C5C5C5`, `=` button accent light 2 (`#4CC2FF`
default) with black text.

Hover/pressed: hover lightens/darkens one step (light: digits `#F9F9F9`,
functions `#F3F5F7`; dark: `+#0A0A0A`), pressed uses secondary text color.
Accent hover 90 % opacity, pressed 80 %.

- Theme follows Windows: `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize`
  `AppsUseLightTheme` (missing = light). React live to
  `Microsoft.Win32.SystemEvents.UserPreferenceChanged`.
- Accent: `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Accent`
  `AccentPalette` (32 bytes, 8 x RGBA: light3, light2, light1, base, dark1,
  dark2, dark3, extra). On this machine:
  `99EBFF 4CC2FF 0091F8 0078D4 0067C0 003E92 001A68 F7630C`.
  Light theme uses dark1 (index 4), dark theme uses light2 (index 1).
  Fallback when missing: `#0067C0` / `#4CC2FF`.
- Native title bar (keeps snap layouts, rounded corners, system menu). Make it
  blend with the client area on Windows 11 via `DwmSetWindowAttribute`:
  `DWMWA_USE_IMMERSIVE_DARK_MODE` (20) in dark mode, `DWMWA_CAPTION_COLOR` (35)
  = background color, `DWMWA_TEXT_COLOR` (36) = text color. Ignore failures
  (Windows 10). Re-apply on theme change.
- Fonts: `Segoe UI Variable Display, Segoe UI` for the big display numbers
  (SemiBold), `Segoe UI Variable Text, Segoe UI` for everything else. Glyphs
  from `Segoe Fluent Icons, Segoe MDL2 Assets` (14 px for the operator, equals and backspace
  glyphs, 15 px for the CE / C / Reset labels, as measured): backspace `U+E94F`, divide
  `U+E94A`, multiply `U+E947`, minus `U+E949`, plus `U+E948`, equals `U+E94E`,
  pin `U+E718`, unpin `U+E77A`.
- Title: `HourCalc`. Default size 340 x 560 (DIP), minimum 300 x 480,
  resizable; the keypad and display scale with the window (star grid). Big
  display text shrinks to fit when long (like Windows Calculator), never
  clipped.
- Window icon: the app icon (same art as the exe icon). Check whether WPF
  picks the exe's Win32 icon by itself; if not, embed the `.ico` as a managed
  resource and set `Window.Icon`.

### Header

Below the title bar, in the place of the Windows Calculator's
"hamburger + Standard" header: two pivot tabs, `Converter` and `Calculator`
(Segoe UI Variable Text SemiBold ~16 px; the selected tab in primary text
color with a 3 px rounded accent indicator under it, the other in secondary
text color). On the right: a keep-on-top toggle (pin glyph; toggles
`Window.Topmost`, tooltip `Keep on top`).

Left of the tabs sits the menu button (section 6). The icon buttons and the tab text share one
centre line, and their glyphs line up with the display's left and right text edges.

The rows that hold the big values and the lines of small text (caption, expression) keep a fixed
height (that of the largest font), so a long value that shrinks its font never moves the keypad.
Two values side by side use one font size (the smaller of the two that fit).

A disabled key shows the disabled text color whatever it holds (a string or a glyph); a pressed key
shows the pressed text color. The disabled color in the dark theme is `#808080`.

Menus and tool tips follow the theme (rounded flyout, `Flyout` / `FlyoutEdge` colors).

Keyboard: `Ctrl+1` / `Ctrl+2` and `Ctrl+Tab` switch tabs.

## 4. Tab 1 - Converter

Display: two fields side by side, equal width.

```
 Hours : minutes          Decimal hours        <- small labels, secondary color
           08:30                   8,50        <- big editable values, right aligned
            8 h 30 min                          <- caption line, secondary color
```

- Both values are editable `TextBox`es styled as display text (no visible box;
  the active field gets a 2 px accent underline, the inactive one none). The
  last focused field is the "active field"; the left field is active at
  start.
- On-screen keypad buttons are not focusable and type into the active field
  at the caret (replacing a selection).
- When a field receives focus by click or Tab, select all its text, so typing
  replaces a computed value. Focus that arrives from outside the window (the window was
  activated again, e.g. after Alt+Tab, or a keypad key was clicked in an inactive window) keeps
  the caret and the typed text.

Keypad (3 columns, like the Windows Calculator converters):

```
 Reset   CE    <backspace>
   7      8      9
   4      5      6
   1      2      3
   :      0      ,
 [         Convert          ]   <- accent button, spans 3 columns
```

- `Reset` clears both fields and the caption, activates the left field.
- `CE` clears the active field (and therefore the computed other field).
- `:` is enabled only when the left field is active, `,` only when the right
  field is active (disabled look like Windows Calculator).
- Keyboard: `Enter` = Convert, `Esc` = Reset.

Conversion rules:

- Live: every edit of a field immediately recomputes the other field from it.
  The edited field is the source. Clearing the source clears the other field.
  This covers "left empty, type 8,50 on the right, left shows 08:30".
- `Convert`: recompute from the active field and also normalize the active
  field's text (`8:3` -> `08:03`, `830` -> `08:30`, `8,5` -> `8,50`,
  `0:90` -> `01:30`). The other field and the caption are computed from what is in
  the active field, not from its normalized text, so Convert never changes them: `8,058`
  shows `08:03` live and the field reads `8,06` after Convert, with `08:03` unchanged.
  A formatted value can have 8 hour digits (`10000000:39`, from `9999999:99`); Convert keeps it.
- Left field input (h:mm): digits plus one separator. Accepted separators
  `:` `.` `u` `h` (Dutch writes 8.30 or 8u30), shown as `:` the moment they are
  typed. `,` is ignored in the left field. At most 2 digits after the
  separator. Digits only: 1-2 digits = whole hours (`8` -> 08:00), 3 or more
  digits = last two are minutes (`830` -> 08:30, `1230` -> 12:30). Minutes
  of 60 or more are accepted and carried (`0:90` = 1 h 30 min).
- Right field input (decimal hours): digits plus one decimal separator; `,`
  and `.` are both accepted and shown as the culture's separator. `:` is
  ignored in the right field. At most 6 decimals typed.
- Output formats: h:mm as `HH:mm` with at least 2 hour digits (`08:30`,
  `123:45`), minutes rounded to the nearest minute (midpoint away from zero);
  decimal with exactly 2 decimals (`8,50`, `8,33` for 08:20), midpoint away
  from zero, culture separator, no thousands separators.
- Caption: the value in words, `8 h 30 min` (`0 h 45 min`, `12 h 0 min`).
- Round trip is exact to the minute: 2 decimals carry at most 0.3 min error.

## 5. Tab 2 - Calculator

Display:

```
                       07:30 + 07:20 =      <- expression line, secondary color
 Hours : minutes          Decimal hours
           14:50                  14,83     <- two big result fields
       14 h 50 min                          <- caption under the left field
```

- Not editable (like the Windows Calculator display). Right-click on either
  value offers `Copy`. `Ctrl+C` copies the h:mm value, `Ctrl+Shift+C` the
  decimal value, `Ctrl+V` pastes into the current entry if it parses.
- Idle state shows `00:00` and `0,00`.

Keypad (4 columns, like Windows Calculator Standard):

```
  CE     C    <backspace>   /
   7     8     9            *
   4     5     6            -
   1     2     3            +
   :     0     ,            =      <- = is the accent button
```

Keyboard: digits (main row and numpad), `:` `u` `h` as time separator, `,`
and `.` (including numpad decimal) as decimal separator, `+ - * /` (and
numpad), `Enter` and `=` for equals, `Backspace`, `Delete` = CE, `Esc` = C.
Use text input (`TextInput`/`PreviewTextInput`) for characters so it works on
AZERTY and QWERTY layouts, and `KeyDown` only for Enter, Backspace, Delete,
Esc.

Values and semantics:

- Internally every value is a `decimal` number of minutes (no floating point).
- An entry is a duration: with `:` it is h:mm (`7:30`, minutes may be >= 60
  and carry), with `,` it is decimal hours (`7,5` = 7 h 30 min), with neither
  it is whole hours (`8` = 8 h). In the calculator there is no "830 means
  8:30" shortcut. One separator per entry, at most 2 digits after `:`, at most
  6 decimals, at most 7 digits before the separator.
- `+` and `-` add/subtract durations.
- `*` and `/` take the second operand as a plain factor: its value in decimal
  hours (`* 2` doubles, `* 1,5` and `* 1:30` both mean x1.5). Duration x
  factor = duration.
- Immediate execution, left to right, like Windows Calculator Standard (no
  operator precedence): `1:00 + 2:00 * 2 =` gives `06:00`.
- Pressing an operator: if there is an entry, fold it into the accumulator
  with the pending operator and show the running result in the display; if
  there is no new entry, replace the pending operator. The expression line
  accumulates the chain (`07:30 + 07:20 + 08:00 +`) showing duration operands
  as `HH:mm` and factor operands as decimal numbers (`07:30 * 2`,
  `07:30 / 1,5`).
- `=`: complete the chain and show `... =` in the expression line. `=` again
  repeats the last operation (`07:30 + 01:00 = =` -> `09:30`, like Windows).
  `07:30 + =` uses the accumulator as the second operand (`15:00`).
- After `=`, typing a digit/separator starts a new calculation; pressing an
  operator continues with the result.
- `CE` clears the current entry. `C` clears everything. Backspace edits the
  current entry only (no effect on a result).
- Negative results are allowed: `07:00 - 08:30 =` -> `-01:30` / `-1,50`,
  caption `-1 h 30 min`.
- Division by zero shows `Cannot divide by zero` in the left field (smaller
  font), clears the right field, disables the operator keys; any digit, `C`
  or `CE` resets (Windows behavior).
- Overflow (|result| >= 10 000 000 hours) shows `Overflow` the same way. This includes a typed
  or pasted entry of that size (minutes up to 99 can be typed: `9999999:60` is `10000000:00`) as
  soon as an operator or `=` follows.
- While typing an entry the left field shows the raw entry text as typed
  (`7:3`, `7,5`, `12`; an entry that starts with a separator gets a leading zero, typed or pasted:
  `0,5`, `0:30`), the right field shows its live decimal value and the
  caption its value in words. After an operator or `=`, both show the
  formatted accumulator.
- Rounding for display only, as in the converter (minutes to nearest, decimal
  to 2 places, midpoint away from zero). Values that are an exact half but not exactly
  representable after a division (`0:05 / 6 * 3` is exactly 2,5 minutes) still round up:
  the display helpers round to 9 places of a minute (12 of an hour) first. The accumulator keeps full precision:
  `07:30 / 4 = * 4 =` gives `07:30`, not `07:32`.

Required examples (must be unit tests):

| keys | left | right |
|---|---|---|
| `7:30 + 7:20 =` | `14:50` | `14,83` |
| `8 + 0:30 =` | `08:30` | `8,50` |
| `7,5 + 7:20 =` | `14:50` | `14,83` |
| `7:30 * 5 =` | `37:30` | `37,50` |
| `7:30 / 4 =` | `01:53` | `1,88` |
| `7:00 - 8:30 =` | `-01:30` | `-1,50` |
| `7:30 + 1:00 = =` | `09:30` | `9,50` |
| `0:90 =` | `01:30` | `1,50` |
| `1:00 + 2:00 * 2 =` | `06:00` | `6,00` |
| `7:30 / 0 =` | `Cannot divide by zero` | (empty) |

Converter examples: `08:30`->`8,50`; `8,50`->`08:30`; `8,5`->`08:30`;
`08:20`->`8,33`; `8,33`->`08:20`; `830`->`08:30`; `8.30`->`08:30`;
`8u30`->`08:30`; `0:45`->`0,75`; `0,1`->`00:06`; `123:45`->`123,75`;
`8,51`->`08:31`; `0:90`->`1,50`.

Unit tests must run in both an `nl-BE` and an `en-US` current culture (the
decimal separator in expected strings follows the culture).

## 6. Advanced menu: decimal places (added by the owner)

Owner: "By default calculate to 2 digits after the comma, but in an
'advanced' menu you can set more digits after the comma."

- Header: a hamburger button (`U+E700`, like the Windows Calculator's
  navigation button) at the far left of the header row, before the tabs. It
  opens a Windows 11 style menu (rounded 8 px, themed light/dark, no WPF
  default chrome) with:
  - a section header `Advanced`
  - `Decimal places` with radio items `2 (default)`, `3`, `4`, `5`, `6`
  - a separator and `About HourCalc` (small dialog or flyout: name, version,
    `Optimey CommV`).
- The decimal-places setting applies to every decimal output in both tabs:
  converter right field (live and on Convert), calculator right field,
  calculator expression line factors stay as typed. Changing it re-formats the
  values currently shown at once (from the full-precision value, not from the
  rounded text).
- h:mm output stays whole minutes regardless of the setting.
- Typed input keeps accepting up to 6 decimals.
- Persistence: `HourCalc.ini` (location: section 6c) with `DecimalPlaces=<n>`
  (written only on change; read at startup; invalid or missing = 2).
- Tests (TimeMath/CalcEngine take the decimal places as a parameter):
  `08:20` -> `8,33` (2), `8,333` (3), `8,3333` (4), `8,33333` (5),
  `8,333333` (6); `7:30 / 4 =` -> `1,88` (2), `1,875` (3), `1,8750` (4);
  `0:01` -> `0,02` (2), `0,0167` (4).

### 6b. Theme: Auto / Light / Dark (added by the owner, 2026-10-10)

Owner: "I don't see a button anywhere to choose the theme: light/dark/auto".

- The hamburger menu gets a new first section, above `Advanced`:
  - a section header `Theme` (same style as `Advanced`)
  - radio items `Auto (follow Windows)` (default), `Light`, `Dark`
  - a separator, then the existing `Advanced` section, separator, `About HourCalc`.
  The menu keeps the existing look; it grows downward. If it does not fit below the button
  on the monitor's work area, it is shifted up so it stays fully visible.
- `Auto` is the current behaviour: the app follows the Windows app theme
  (`AppsUseLightTheme`) live. `Light` and `Dark` force that theme and ignore later changes of
  the Windows app theme. In every mode the accent colour keeps following Windows live (light
  theme uses accent dark 1, dark theme accent light 2, as in section 3).
- Choosing an item applies at once to everything that is open: main window, title bar (DWM
  attributes), open menus, tool tips, the About and license windows.
- Persistence in `HourCalc.ini` (location: section 6c), key `Theme=auto|light|dark` (key and value
  case-insensitive; missing or invalid = auto). The file holds all settings: whenever a setting
  changes the file is rewritten canonically as
  `DecimalPlaces=<n>\r\n` `Theme=<auto|light|dark>\r\n` (lower-case value), so changing one
  setting never loses the other. Written only on a change, as before; a write failure keeps the
  setting for the session only.
- The C# reference (`oracle\Settings.cs`) gets the same parse and save rules so the
  differential tests keep covering the settings file; `hc_core.h` gets the matching API. The UI
  changes one setting with `SavePlaces` / `SaveTheme`, which read the file again and keep the other
  setting as it is on disk (so a second running HourCalc's change is not lost; the caller's own copy of
  the other setting is used only when there is no file), and write nothing when the file is there but
  cannot be read; `Save` writes both. The file is replaced in one step (a temporary file, then a rename),
  so a reader never sees half a file.

### 6c. Where the settings live (owner, 2026-10-10)

Owner: "put the settings in Windows temp so no extra files appear next to the .exe" and
"make sure the tool can write its settings without admin rights".

- `HourCalc.ini` lives in `%LOCALAPPDATA%\HourCalc\` (per user, always writable without admin
  rights; the folder is created on the first save). Nothing is ever written next to the exe.
- Not `%TEMP%`: measured on the owner's PC, Storage Sense is on with temporary-file cleanup
  every 7 days, so settings there would silently disappear.
- Resolve the folder with `SHGetKnownFolderPath(FOLDERID_LocalAppData)` (C#:
  `Environment.SpecialFolder.LocalApplicationData`); when it cannot be resolved or written,
  settings apply to the session only, as before.
- Implementation note (measured, 2026-10-10): the native code takes the folder from the environment
  variable `LOCALAPPDATA` (Windows sets it for every session) when that holds an absolute path, and
  has no folder otherwise (the settings then last for the session, as above), instead of calling
  `SHGetKnownFolderPath`: the first call of a process loads the shell storage DLLs, which cost 22 ms and
  thirteen more modules (one of them a third-party shell extension) at every start. It is the same
  folder; the tests check that the variable of this PC and `SHGetKnownFolderPath` agree.
- No migration needed: no `HourCalc.ini` next to any exe exists on the owner's PC (checked).

## 7. Icon

The owner picked proposal D4 from the 20 proposals (`logos\round1\D4`): a disc
cut into four quarter-hours by a plus-shaped gap, three quarters solid and the
fourth an outline (45 min = 0,75 h), with a white clock hand to 12. It is used
in the blue of proposal D3 (gradient `#28C4E6` -> `#0B7FAF`, solid `#129DC4` at
small sizes). The art lives in `assets\icon\`:

- `app.svg` - the main drawing (the D4 geometry with D3's gradient), used for
  the 40, 48, 64 and 256 px frames;
- `app-16.svg` - a 16 px grid drawing (every edge a multiple of 16 units), used
  for the 16, 20 and 32 px frames (32 is exactly twice the 16 px grid);
- `app-24.svg` - a 24 px grid drawing (1 px = 256/24 units), used for the 24 px
  frame, the taskbar size at 100 % scaling;
- `app.ico` - built from these by
  `python tools\make_ico.py assets\icon\app.svg --small assets\icon\app-16.svg --size 24=assets\icon\app-24.svg assets\icon\app.ico`.

The recoloured proposal, its generator and a check strip live in `logos\final\`.
The icon must appear as the exe icon (Explorer), window icon (title bar) and
taskbar icon.

## 8. License: GPL-3.0-or-later (added by the owner)

Same model as the owner's Background Editor: GPL-3.0-or-later, copyright
`Optimey CommV`, REUSE-compliant.

- `LICENSE` (root) and `LICENSES\GPL-3.0-or-later.txt` are already in place:
  the verbatim FSF text, SHA-256
  `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`, LF.
  Do not edit them.
<!-- REUSE-IgnoreStart -->
- Every source file starts with SPDX headers:
  `// SPDX-FileCopyrightText: 2026 Optimey CommV` and
  `// SPDX-License-Identifier: GPL-3.0-or-later` (`#` for .py/.ps1, `<!-- -->`
  for .svg/.manifest).
<!-- REUSE-IgnoreEnd -->
  Files that cannot carry a header (png, ico, md, txt in
  logos) are covered by a `REUSE.toml` at the root with the same license and
  copyright. The generated logo proposals under `logos\` are covered by it as well. `docs\reference\wincalc-light.png` is a screenshot of Microsoft's
  Calculator kept only as a design reference: annotate it in `REUSE.toml` as
  `LicenseRef-Reference-Only` with copyright Microsoft Corporation, add
  `LICENSES\LicenseRef-Reference-Only.txt` explaining it is not distributed
  with the program, and exclude it from the source zip.
- Assembly attribute copyright: `Copyright (c) 2026 Optimey CommV`.
- The exe carries its own license and source, so a copied-around portable exe
  stays compliant:
  - `build.ps1` first creates `dist\HourCalc-<version>-source.zip` with what
    is needed to build the exe (src, assets, tools\make_ico.py, build.ps1,
    README.md, LICENSE; no tests, review tools, logo rounds or design
    documents, to keep the exe small) and embeds that zip as a managed
    resource. The About window reads the license text from it.
- `About HourCalc` (Advanced menu) shows the appropriate legal notices (GPL
  section 0/5d): name and version, `Copyright (c) 2026 Optimey CommV`, and
  "This program is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation, either version 3 of the License, or (at your option)
  any later version. This program comes with ABSOLUTELY NO WARRANTY." Two
  buttons: `View license` (shows the embedded full text in a scrollable,
  themed window) and `Save source code...` (SaveFileDialog, writes the
  embedded zip).
- README gets a License section.
