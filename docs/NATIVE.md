# HourCalc native port (C++ / Win32) - design and acceptance

> **Note for the public repository.** This is a working document of the development. Material it
> mentions that was only working material is not published: the earlier WPF build (`legacy\wpf\`),
> its snapshots (`docs\snapshots\`), the snapshot renders and comparisons, the logo proposals
> (`logos\`) and a reference screenshot of Microsoft's Calculator.

This document overrides SPEC.md sections 1 and 2 (technology and code structure).
SPEC.md sections 3-8 (look, converter, calculator, Advanced menu, icon, license) stay
the specification of the behaviour, and the WPF build is the visual and behavioural
reference: its code is in `legacy\wpf\`, its snapshots in `docs\snapshots\`.

## Why (measured on the developer's laptop, 2026-10-10)

Time from process start to a visible window, warm runs:

| variant | time |
|---|---|
| HourCalc WPF (.NET 4.8) | 1.4 - 2.5 s |
| empty WPF window | 1.0 - 1.8 s (0.7 - 1.5 s of it inside the first `Window.Show`, D3D9 + Intel driver init) |
| empty WinForms window | 0.4 - 0.8 s |
| native Win32, Direct2D hardware | 1.0 - 1.9 s (loads dxgi/d3d11 + Intel driver) |
| native Win32, Direct2D *software* | 1.0 - 1.7 s (still loads dxgi/d3d11/WARP) |
| native Win32, GDI+ shapes + DirectWrite text via GDI interop | **0.16 - 0.17 s**, loads only gdiplus.dll and DWrite.dll |

Anything that touches DXGI/Direct3D costs about a second on that laptop. So: native Win32, and no
DXGI anywhere.

## Acceptance criteria (all measured, none assumed)

1. **Startup**: median of 5 warm runs <= 250 ms from process start to the first painted,
   visible window, measured with `tools\starttime.ps1` on this PC.
2. **No DirectX**: the running process never loads `dxgi.dll`, `d3d*.dll`, `d2d1.dll` or
   `D3D10Warp.dll` (check the module list after the first paint and after opening every
   popup/dialog).
3. **Size**: `dist\HourCalc.exe` <= 200 KB including the embedded source zip, the icon and the
   signature. Single file, no DLLs to ship, runs on a clean Windows 10 1809+ / 11 x64 (no VC++
   redistributable: link the CRT statically, or use the "hybrid CRT" - static vcruntime with
   the system UCRT - if that is smaller and verified to run).
   Measured (`docs\SIZE.md`, where every byte is attributed): 302.5 KB with the embedded source zip (the
   zip alone is 120 KB, so 200 KB is out of reach with it), 191.5 KB without it
   (now the default; `build.ps1 -EmbedSource` gives the embedded variant: the license text from a small
   deflated resource, the address of the source code instead of the "Save source code..." button). Both
   unsigned. With the theme choice (SPEC 6b) the default build is 199,168 bytes (194.5 KB), still under 200,000. The owner chose the variant without the embedded zip on 2026-10-10; the source is published
   on GitHub next to the exe.
4. **Same behaviour**: every string the core produces equals the C# reference
   (`oracle\*.cs`) for the same input - proven by `tests\Differential.cs` over all example
   tables, exhaustive converter inputs and >= 1 000 000 random calculator key sequences, in
   a ',' and a '.' decimal-separator culture. Every UI behaviour of SPEC 4-6 and of the WPF
   build (focus/select-all rules, keypad enable state, keyboard mapping incl. AZERTY text
   input, copy/paste, menu, About, license viewer, save source, keep on top, theme follow,
   DPI change, resize/fit) works the same.
5. **Same look**: the native snapshots (same names as `docs\snapshots\*.png`, same states and
   sizes) match the WPF snapshots: same layout to within 1 px at 100 % (2 px at 150 %), same
   colours (exact for fills; text anti-aliasing may differ), same fonts (Segoe UI Variable
   Display/Text with the same weights, Segoe Fluent Icons glyphs, fallback Segoe UI / Segoe MDL2
   Assets). Text is sharp (DirectWrite, ClearType or grayscale like WPF), shapes anti-aliased.
6. **GPL**: GPL-3.0-or-later, SPDX headers on every source file, the exe embeds its source zip
   (what is needed to build it: native\, assets\, tools\make_ico.py, tools\msvc.ps1, build
   scripts, README.md, LICENSE) and shows the license from it; About as in SPEC 8.
7. **Portable**: single exe, no registry writes, nothing written next to the exe; the only file written is
   `%LOCALAPPDATA%\HourCalc\HourCalc.ini`, on a settings change (SPEC 6c).

## Architecture

```
native\core\hc_core.h     the contract between core and UI (given; do not change the public API
                          without updating both sides)
native\core\*.cpp         port of oracle\TimeMath.cs, Converter.cs, CalcEngine.cs, Settings.cs
                          (one .cpp per .cs), Dec = an own System.Decimal on 32-bit limbs (dec.cpp, also used on
                          Linux, no oleaut32), the operating-system calls in platform_win.cpp (platform_posix.cpp
                          is the Linux half, see LINUX.md); no UI, no globals beyond
                          the separator override. Settings.cs is HourCalc.ini in %LOCALAPPDATA%\HourCalc:
                          DecimalPlaces and Theme (auto|light|dark), read together (Load) and written
                          together, always with both lines, by a temporary file that replaces the file in
                          one step (Save); a changed setting goes through SavePlaces / SaveTheme, which
                          read the file again for the other setting, so one setting never loses the other,
                          also not across two running programs, and a file that cannot be read is not
                          overwritten with defaults
native\cli\hc_cli.cpp     console front end of the core for differential testing (line protocol); only built
                          with the tests and not in the source archive
native\app\*.cpp/.h       the Win32 app: window, layout, rendering, widgets, menus, dialogs, theme
native\app\app.rc         icon (assets\icon\app.ico), VERSIONINFO, manifest, RCDATA source zip
native\app\app.manifest   PerMonitorV2, supportedOS Win10/11, asInvoker, Common Controls v6
tests\native\core_tests.cpp   C++ unit tests: every example table of SPEC 4, 5, 6
tests\Differential.cs     C# driver: oracle vs hc_cli.exe (or, with --cli "<command line>", vs another build of it: Linux)
tests\Tests.cs            existing C# unit tests of the oracle (keep passing)
build.ps1                 orchestrates build-core.ps1 + build-app.ps1, tests, -Sign, -EmbedSource, size report
build-core.ps1            core lib (-SkipTests: only that), hc_cli.exe, core_tests.exe, then tests\core-tests.ps1
tests\core-tests.ps1      core_tests.exe, oracle tests, differential tests (dot-sourced by build-core.ps1)
build-app.ps1             HourCalc.exe (release) and HourCalc-test.exe (HC_TESTHOOKS); no source zip in the exe unless -EmbedSource
tools\mapsize.py          attributes the bytes of the exe to objects and libraries from a linker map (docs\SIZE.md)
tools\msvc.ps1            dot-source to load the MSVC x64 environment (given, tested)
tools\starttime.ps1       startup timing harness
```

Toolchain (installed, verified): Visual Studio Build Tools 2022, MSVC 14.44, Windows SDK
10.0.26100. C++17. Compile with `tools\cflags.rsp` (`/O1 /Oi /GL /Gy /Gw /GR-` and
`/D_USE_STD_VECTOR_ALGORITHMS=0` for size, plus `/Ob0` for `app_layout.cpp` and `app_window.cpp`, see
`docs\SIZE.md`) and keep `/GS` on (never `/GS-`); link with
`/LTCG /OPT:REF /OPT:ICF /NOCOFFGRPINFO /MERGE:.pdata=.rdata`.
No third-party libraries (write small pieces yourself: zip reading, inflate if used, PNG output
for the test renders through GDI+). Unicode (`UNICODE`, `_UNICODE`) everywhere.

### Rendering (decided, measured)

- One memory DIB per window (32-bit), the whole client painted into it, then one BitBlt.
  `WM_ERASEBKGND` returns 1. No flicker on resize.
- Shapes (rounded keys, borders, underline, pills, menu backgrounds): GDI+ with
  anti-aliasing on that DIB. Keep a GDI+ object cache; create brushes/pens per theme change,
  not per paint.
- All text and icon glyphs: DirectWrite through `IDWriteGdiInterop::CreateBitmapRenderTarget`
  (text layout -> custom `IDWriteTextRenderer` -> `IDWriteBitmapRenderTarget::DrawGlyphRun`)
  drawn into the same DIB, so text uses the same font engine as WPF (Segoe UI Variable with real
  weights via `IDWriteFactory` font axes or the named instances; Segoe Fluent Icons glyphs).
  Measure text with `IDWriteTextLayout` for the shrink-to-fit logic.
- No D2D, no DXGI, no Direct3D, no WIC (WIC can pull in DXGI; PNG test output through GDI+).
- Popups (Advanced menu, Copy menu, tooltips, About, license viewer): own windows painted the
  same way; Windows 11 rounded corners via `DWMWA_WINDOW_CORNER_PREFERENCE`, themed colours,
  shadow like WPF build. License text: a read-only multi-line EDIT control is acceptable if
  it is themed (colours via WM_CTLCOLOR*, dark scrollbars via `SetWindowTheme(h, L"DarkMode_Explorer", nullptr)`);
  otherwise a custom scroll view.
- Title bar: native, DWM attributes exactly as SPEC 3 (immersive dark mode, caption colour,
  text colour), re-applied on theme change.
- Theme choice (SPEC 6b): `MainWindow` holds the mode (`hc::settings::ThemeMode`: Auto, Light, Dark; loaded
  from `HourCalc.ini`, saved with `settings::SaveTheme` only when it changes; the places with `SavePlaces`). The
  change is painted at once (`RepaintNow`, an `UpdateWindow`) before the file is written. `theme::IsDarkFor(mode)`
  gives light or dark (Auto reads `AppsUseLightTheme`, the others force it), the accent always comes from
  Windows (`SystemAccent(dark)`). `WM_SETTINGCHANGE` / `WM_THEMECHANGED` re-run that, so Auto follows Windows
  live, a forced theme only follows the accent. A change goes through `MainWindow::SetPalette`: the DWM
  attributes, the menu, Copy menu, field menu and tool tip (an open popup is painted again at once, see
  `Flyout::SetPalette`) and every live About / license window (`Dialog::SetPaletteOfAll`). The menu has
  two radio groups (`Flyout::Item::group`), the Theme items and the decimal places items; `Flyout::Fit`
  keeps a popup inside the work area (the menu, 441 px tall at 100 %, moves up when it does not fit below
  its button). A click inside a popup's box that is not the press of an enabled item (the header, a label, a
  separator, the padding, a greyed out item, a right click) does nothing and the popup stays open, like the
  WPF `Popup` with `StaysOpen = false`; only a click outside the box closes it.
- Layout: port the WPF layout math from `legacy\wpf\src\MainWindow.cs` (base client 324x521
  DIP, scale clamp 0.85..2.0, key margins, header 48, fit/shrink rules, partner sizes, min row
  heights) and `Styles.cs`/`Theme.cs` (colours, hover/pressed/disabled, accent from
  AccentPalette, UserPreferenceChanged -> `WM_SETTINGCHANGE` "ImmersiveColorSet").
- DPI: PerMonitorV2; on `WM_DPICHANGED` use the suggested rect, rebuild fonts/brushes.

### Input

- Converter value fields are custom single-line editors (caret, selection, Shift+arrows,
  Home/End, Ctrl+A, Ctrl+C/X/V, mouse click/drag select, double-click select all, caret blink
  with the system blink time), right-aligned, with the WPF build's filtering and select-all
  rules (`legacy\wpf\src\MainWindow.cs`). Typed characters come from `WM_CHAR` (layout
  independent); Enter/Esc/Backspace/Delete/Tab/Ctrl+1/Ctrl+2/Ctrl+Tab from `WM_KEYDOWN`.
- Keypad keys do not take focus (as in WPF); mouse capture for press/release like a button;
  hover tracking via `TrackMouseEvent`.
- Accessibility: keyboard focus visuals as in the WPF build; set window and control names so
  Narrator at least reads the window title (full UIA is out of scope).

### Test hooks (test build only)

`HourCalc-test.exe` is compiled with `HC_TESTHOOKS` from the same sources; the release exe has
no hooks. It supports:

- `--render <dir>`: renders every state of the WPF snapshot list into `<dir>` with the same
  file names (light and dark, sizes, 150 % DPI, menus/dialogs/tooltips), without showing any
  window (create windows hidden, call the paint routine into the DIB, save PNG via GDI+). It also
  renders the theme choice (`forced-dark-*`: Dark forced on a light Windows; `forced-light-*`: Light
  forced on a dark Windows; converter, calculator and menu). `light-menu` and `dark-menu` have the new
  Theme section and no WPF reference (the WPF build has no theme choice); the snapshots of the
  other states are unchanged by it, and the `*-about` snapshots of `docs\snapshots-native` are those
  of the build with the embedded source (the render of the default build is 27 px taller: the line
  with the address of the source code).
- `--renderdpi <dir>` and `--geometrydpi <file>`: a few states at 100, 125, 150, 175 and 200 % (images and the
  layout in device pixels), to compare with a WPF layout made with `VisualTreeHelper.SetRootDpi`.
- `--selftest`: UI-level behaviour tests driven through the real window procedures with
  synthetic messages on hidden windows (typing via WM_CHAR, keys via WM_KEYDOWN, focus changes,
  keypad clicks via the key handlers), port of the behaviour checks in
  `legacy\wpf\tests\UiTests.cs` and the window parts of `legacy\wpf\tests\ReviewTests.cs`.
  Exit code 0 only when all pass. The theme choice is tested against a simulated Windows
  (`theme::SetSystemForTest`: the app theme and the two accent colours that Windows would report, so
  nothing of the machine's settings is touched): the menu (items, order, radio state), choosing each
  item (the pixels of the background, a function key, a digit key and the accent key against SPEC 3,
  the title bar: the dark mode switch read back from DWM, the colours as `ApplyCaption` set them),
  the settings file round trip (both keys survive a change of either, invalid values), Auto
  following a simulated change of the Windows theme and accent while Light and Dark follow the
  accent only, the About and license windows following, and the menu position near the bottom
  of a work area (given to `MenuPosition`, nothing is shown). Popups are marked open without being shown
  (`Flyout::OpenHiddenForTest`, test build only) and get synthetic clicks (a click inside the box that is
  no item leaves the menu open, a click outside closes it) and a theme change (an open popup is painted
  again at once, a closed one is not, a message that changes nothing costs no repaint; `Flyout::Presents`,
  `MainWindow::InvalidateCount`). Also: the settings file with two windows on it, a file that is locked
  at start-up, the first save making the folder, the change being on the screen before the file is
  written (`RepaintNowCount`, `RepaintNowCountAtLastSave`), and a default path that is found without
  loading the shell's storage DLLs.
- `--measure-startup --popups`: shows the window and drives its popups with messages (menu, click-through,
  move and resize, Cut / Copy / Paste menu, tool tip); a watchdog ends it after a minute whatever happens.
- `--measure-startup`: shows the window without activating it (`SW_SHOWNOACTIVATE`), prints
  the time from process start (`GetProcessTimes`) to the end of the first `WM_PAINT`, the list
  of loaded modules, then exits. Agents use this instead of launching the release exe, so no
  focus is stolen from the owner.

## Not allowed

- Showing windows on the owner's screen other than through `--measure-startup` (at most 10
  runs per agent), sending input to other processes, browser tools, downloads, installs.
- Changing `oracle\*.cs` semantics (it is the reference) or `docs\SPEC.md` behaviour.
