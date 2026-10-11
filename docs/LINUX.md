# HourCalc for Linux (.deb) - design and acceptance

> **Note for the public repository.** This is a working document of the development. Material it
> mentions that was only working material is not published: the earlier WPF build (`legacy\wpf\`),
> its snapshots (`docs\snapshots\`), the snapshot renders and comparisons, the logo proposals
> (`logos\`) and a reference screenshot of Microsoft's Calculator.

Decided by the owner (2026-10-10): amd64 only; Debian 13 (trixie) and the latest Ubuntu LTS
(26.04 "resolute", measured with `wsl --list --online`); desktops GNOME (GTK) and KDE (Qt);
**native per desktop**: GTK4 + libadwaita on GNOME, Qt6 Widgets on KDE, the same structure
(tabs, two display fields, keypad, menu) and the same behaviour, but the desktop's own widgets,
fonts, colours, accent and light/dark. GPL-3.0-or-later, later published together with the
Windows version on GitHub.

SPEC.md sections 4, 5, 6, 6b and 8 are the behaviour specification on Linux too; section 3 (the
Windows 11 look) and the Windows-specific parts of 6c do not apply. NATIVE.md stays the Windows
design. Rule for every tool on this PC: measure startup and loaded libraries before calling it done.

## Build environment (measured 2026-10-10)

| distro (WSL) | GTK | libadwaita | Qt |
|---|---|---|---|
| Debian 13 trixie | 4.18.6 | 1.7.6 | 6.8.2 |
| Ubuntu 26.04 LTS resolute | 4.22.4 | 1.9.0 | 6.10.2 |

Both WSL distros have the build packages (build-essential, cmake, meson, ninja, pkg-config,
libgtk-4-dev, libadwaita-1-dev, qt6-base-dev, debhelper, lintian, xvfb, desktop-file-utils,
appstream, fonts). Code must build and run on the OLDER versions (Debian: GTK 4.18, libadwaita 1.7,
Qt 6.8); use newer API only behind version checks.

WSL has `processors=8` (hard rule in CLAUDE.md: never more). Agents run commands in WSL with
`wsl.exe -d Debian -- ...` / `wsl.exe -d Ubuntu -- ...` as the normal user; **never `-u root`**
(the owner allowed root only for installing the build packages, which is done). No `sudo`.

Sources live on K: (the NAS), which WSL cannot see. Build inside WSL from a copy:
`tools\wsl-sync.ps1 -Distro Debian|Ubuntu` mirrors the repository (without obj, dist, logos,
legacy, docs\snapshots*) to `~/hourcalc-src` in that distro and restores the executable bits that a
Windows copy loses (debian/rules, *.sh). Build output stays in WSL (`~/hourcalc-build-*`);
finished .debs are copied back to `dist\linux\<distro>\`.

**Never show a window on the owner's screen**: WSLg would put Linux windows on his Windows
desktop. GUI tests run headless only: GTK under `xvfb-run -a` with `GDK_BACKEND=x11`
(unset WAYLAND_DISPLAY), Qt with `QT_QPA_PLATFORM=offscreen`. Test binaries never fall back to
the normal GUI on an unknown option (exit code 2).

## Architecture

```
native/core/            the shared core (Windows and Linux): made portable, see below
native/app/             the Windows Win32 UI (unchanged except where the core API changes)
linux/gtk/              GTK4 + libadwaita front end  -> hourcalc-gtk
linux/qt/               Qt6 Widgets front end         -> hourcalc-qt
linux/data/             desktop file, AppStream metainfo, icons (hicolor), launcher script
linux/tests/            Linux test drivers (headless selftests, render to PNG, startup timing)
meson.build             Linux build: core library, hc_cli, core_tests, both front ends, data
debian/                 Debian packaging (source format 3.0 (native) or quilt, debhelper compat 13)
tools/wsl-sync.ps1      copy the repo into a WSL distro for building
```

### 1. Portable core (owner of `native/core`)

- `hc::Dec`: replace the oleaut32 wrapper with an own portable implementation of the
  System.Decimal arithmetic (96-bit mantissa, scale 0-28, sign; add, sub, mul, div, compare,
  rounding, truncate, parse, format) used on BOTH platforms. It must give the same results as the
  C# reference (System.Decimal on .NET Framework 4.8) - the existing differential suites prove it,
  including the Dec suite (3.6 M checks) and the known division limit near 2^96 (match the reference
  there too if possible, otherwise document). No compiler-specific 128-bit types that MSVC lacks
  (use 32/64-bit limbs).
- `hc_core.h` becomes platform neutral (no windows.h / oleauto.h). Keep the public API;
  std::wstring stays (wchar_t is UTF-16 on Windows, UTF-32 on Linux; the front ends convert).
- Platform layer: `native/core/platform_win.cpp` and `platform_posix.cpp` for the decimal
  separator (Windows: GetLocaleInfoEx; Linux: the LC_NUMERIC radix of the user's locale), the
  settings path and atomic file write. Linux settings path: `$XDG_CONFIG_HOME/hourcalc/hourcalc.ini`
  (default `~/.config/hourcalc/hourcalc.ini`), same file format and rules as SPEC 6/6b/6c (the
  single-key saves, the tri-state read, the atomic replace via a temp file + rename(2)).
- Windows must stay exactly as it is: full `build.ps1` green (all differential suites 0
  mismatches, selftest), exe size not larger, startup not slower (median of the hidden
  `--measure-startup --hidden` runs; no on-screen runs).
- Linux: `meson.build` builds `libhccore` (static), `hc_cli`, `core_tests` with g++; the
  differential suites must also run against the Linux `hc_cli` on both distros: give
  `tests\Differential.cs` and `tests\DifferentialReview.cs` an option to start the CLI through a
  command line such as `wsl.exe -d Debian -- ~/hourcalc-build-debian/hc_cli` and run them.
  0 mismatches on Windows, Debian and Ubuntu.

**Status of the core (done and measured, 2026-10-10).**

- Files. `native/core/dec.cpp` (the own `hc::Dec`), `platform.h` (internal: the calls that differ per system),
  `platform_win.cpp` and `platform_posix.cpp` (decimal separator, settings path, file read, atomic write, UTF-8 decoding,
  case-less compare), `meson.build` (`libhccore`, the dependency object `hccore_dep` for the front ends, and with
  `-Dtests=true` `hc_cli`, `core_tests`, `review_state_cli`). `hc_core.h` includes no platform header any more. The public API
  is unchanged except that `Dec::Raw()` is gone and that `settings::FileName` / `FolderName` are `hourcalc.ini` /
  `hourcalc` on Linux. `native/cli/hc_cli.cpp` and `tests/native/*.cpp` build on both systems. `build-core.ps1` leaves out
  `platform_posix.cpp`; `build-app.ps1` links neither oleaut32 nor delayimp any more (delayimp only with `-EmbedSource`,
  for comdlg32).
- `hc::Dec` is System.Decimal on 32-bit limbs, bit for bit (scale included) the arithmetic of .NET Framework 4.8,
  checked against the real `decimal` on top of the suites by a private generator of more than 10 million additions,
  subtractions, multiplications and divisions on the final code (three seeds of 3.8 million, 0 mismatches; the oleaut32
  wrapper had about 115,000 in the same cases), in which operands near 2^96, powers of ten and ties were over-represented.
  It is exact where the oleaut32 wrapper was not (a division whose mantissa lies within 2^37 of 2^96), so the review test
  has no "known limit" any more. Quirks of the runtime that the code copies, each with a unit test: a zero product keeps
  its scale only when both mantissas have 32 bits or less (otherwise it is a plain 0), a product that drops 20 or more
  digits is a plain 0 in that quick path, a result that needs one digit more than the estimate is rounded once, a
  quotient whose rounding would carry out of 96 bits is rounded one digit earlier, and (found in the review of the Linux
  version, 2026-10-11) the rounding of a quotient whose divisor has more than 64 bits: the runtime shifts the divisor left
  until its top bit is set, doubles the remainder and compares it with the divisor limb by limb, and the carry out of a
  low or middle limb that holds exactly 0x80000000 gets lost in the doubling. So a quotient that is a hair above one half
  rounds down when the remainder has such a limb (it takes a remainder within a few units of exactly one half: the
  review found nine cases among 3 million random near-half divisions), and an exact half rounds down for an odd quotient
  as well. `RoundsUpWide` in
  `dec.cpp` copies it; it fits the real decimal on 1.9 million built and random near-half divisions (limb values 0, 1, 2^31 - 1,
  2^31, 2^31 + 1, 2^32 - 1 and random) with no exception, where the exact comparison differs in 5,682 of them. Divisors of
  64 bits or less never reach it, and the app only divides by 60 and by a number the user typed (at most 7 + 6 digits, a
  mantissa below 2^44), so nothing the app computes is affected.
  `tests\Differential.cs` has a suite of built near-half divisions for it ("Dec division, divisor over 64 bits"), and
  `core_tests` the cases themselves.
- Linux settings. `$XDG_CONFIG_HOME/hourcalc/hourcalc.ini` (an absolute value only), else `$HOME/.config/hourcalc/hourcalc.ini`
  (HOME from the environment or the password database). The same file format as on Windows, CR LF included, so the file is
  byte for byte the same on both. The first save also makes `~/.config` (0700) when that is the one folder missing; a
  longer chain of missing folders is not made, as on Windows. Read-only is "no write permission bit at all" (the Windows
  attribute); the mode of the file stays after a save; a symbolic link to the file is followed; the temporary file is
  `<file>.<pid letters>.tmp`, fsync'ed, then `rename(2)`. A file that exists but cannot be read (no permission, a folder,
  a FIFO) is never overwritten. File names keep non-UTF-8 bytes (U+DC80..U+DCFF stand for them in the wide string).
  The decimal separator is asked from a `newlocale()` object of its own, so a program whose global `LC_NUMERIC` is "C"
  (Qt) still gets the user's separator; without an installed locale it is ".". The core never calls `setlocale`.
- Tests. `core_tests`: 1,739 checks on Windows, 1,826 on Debian 13 and 1,823 on Ubuntu 26.04 (the settings checks differ
  per system: attributes and locked files on Windows, permission bits, XDG paths, symbolic links and locales on Linux;
  the locale checks skip a locale that is not installed, `LOCPATH=<folder made by localedef>` adds some without root).
  `tests\Differential.cs` and `tests\DifferentialReview.cs` take `--cli "<command line>"`, for instance
  `--cli "wsl.exe -d Debian --cd / -- /home/<user>/hc-core-build/native/core/hc_cli"` (at most 6 workers then). hc_cli
  answers `os` (windows|linux) and, on Linux only, `fs.*` and `env.set` commands, with which the tests reach the files
  of the program under test and replace the Windows-only checks (locked file, file attributes, `%LOCALAPPDATA%`) by their
  Linux counterparts. `tests\linux-core-tests.ps1 -Distro Debian|Ubuntu [-Quick] [-Full]` does everything: sync, meson,
  core_tests, the three differential runs. Results (the default suite in full, the review test as a sample with
  `--scale=0.05 --cultures=2`, and with the private state of the calculator): Windows 24,282,067 / 14,049,356 / 453,078
  checks, Debian 13 and Ubuntu 26.04 the same numbers, all 0 mismatches (re-run in full on 2026-10-11 after the review fixes). (Time with 6 workers through `wsl.exe`: the default
  suite 7 to 9 minutes, the review sample 2 to 5, the state run 0.6 to 1.5; Windows with 10 to 12 workers: 6, 3.5 and 1.)
  `core_tests` also passes under AddressSanitizer + UBSan with halt_on_error (Debian), and so does the Dec suite of the
  differential test (1,183,068 checks of the quick run).
- Windows. Full `build.ps1`: everything green; `dist\HourCalc.exe` 199,168 bytes, the size it had before the portable
  `hc::Dec` (198,656 with that alone: OLEAUT32.dll is no longer imported; the wide-divisor rounding below is 512 bytes of
  the file; `docs\SIZE.md` has the sections), hidden start-up the same within the noise of the machine (median 197 against
  194 ms under load, 80 to 90 against 85 ms idle). (Review of 2026-10-11: the retry of the rename in a settings save, `platform_win.cpp`, now lasts up to
  500 ms instead of 160 ms. On this machine a real-time scanner kept the settings file from being replaced for longer
  than 160 ms in about one save in seventy of a plain loop that saved and read the file, which made the settings walk of
  the review differential fail two or three times in a run; with 500 ms 3,000 such saves gave no failure. A read-only
  file or a folder, which never work, now fail after half a second.)
- The reference itself can crash. The decimal division of .NET Framework 4.8 ends the process (exit code 127, or 0xC0000409
  from a shell; no exception) for some overflowing quotients, with divisors of one, two or three limbs (mostly with a large
  divisor scale), for example `-79228162514264337593543950323m / 0.0999999999999999999999999998m` (three limbs),
  `-792281625142643375935443 / -0.0000000000000000000010000` (one limb) and `-7922816251426433759354395.034 /
  0.0000000000100000000000000000` (two limbs). `hc::Dec` answers `!OVERFLOW` for all of them. No case of the fixed suites
  does that. A generator must skip every quotient that overflows (or run the reference once per case in a child process):
  skipping only the divisors of three limbs does not help.

### 2. GTK4 + libadwaita front end (`linux/gtk`, binary `hourcalc-gtk`)

- C++17 on the GTK/libadwaita C API (no gtkmm). AdwApplication + AdwApplicationWindow,
  AdwHeaderBar with an **AdwInlineViewSwitcher** (Converter / Calculator, labels only) as title
  and the primary menu (hamburger, GMenu: Theme radio Auto/Light/Dark, Decimal places 2-6, About)
  at the end, as the GNOME HIG wants. Content: two display fields side by side with labels and
  caption, keypad grid of buttons (libadwaita styles; Convert and = as `suggested-action`), the
  same keys, rules, keyboard handling, select-all, copy/paste, error states, expression line,
  decimal places as SPEC 4-6. Theme via AdwStyleManager (Auto = default color scheme, Light/Dark =
  forced); accent follows the system (libadwaita >= 1.6). About: AdwAboutDialog with
  GPL-3.0-or-later, version, copyright Optimey CommV, website = the GitHub URL. "Keep on top" does
  not exist on GNOME/Wayland: left out on GTK.
- **Decisions of the build that differ from the first draft of this file** (decided in the review
  of 2026-10-11):
  - The header title is an AdwInlineViewSwitcher, not an AdwViewSwitcher (libadwaita >= 1.7, which
    is Debian 13, the oldest target; `meson.build` requires it). The wide AdwViewSwitcher with icons
    needs about 330 px next to the menu button and the window buttons, and its labels were cut off at
    this window width; without icons it shows a broken-image icon. libadwaita presents the inline
    switcher as the in-content switcher, so this is a judgement call that the owner can overrule
    (the alternative is icons on the two pages and the narrow-policy AdwViewSwitcher); it renders
    well on both distros and nothing clips. With the Ubuntu-style decoration layout
    (`appmenu:minimize,maximize,close`) the header needs 371 px, so the minimum content width then
    grows from 360 to 371 (nothing clips).
  - The minimum window size is 360 x 480 (default 360 x 600), not the 300 px of Windows: libadwaita's
    About dialog overflows and is clipped in a narrower window. On X11 without a compositor the
    content is about 10 px smaller than the window (the decoration shadow), so the renders show
    350 x 590 and 350 x 470.
  - The renderer: see Startup below.
- Keyboard and menus (all with a headless test): Ctrl+1 / Ctrl+2 are the keys of the number row,
  not the characters, so they work on a Belgian or French AZERTY layout (where the digits are on the
  shifted level) with Ctrl alone, as on Windows (`IsKeyOf` in `hcwindow.cpp`: the hardware key code
  of the keyval, from `gdk_display_map_keyval`); Ctrl+Q flushes a settings write that is still
  waiting (the application's `shutdown` signal; closing the window already did); opening and closing the
  primary menu or About leaves the caret and selection of the converter field as they were; the
  Menu key and Shift+F10 open the Cut / Copy / Paste menu of the focused field; the right button on
  a calculator value opens the Copy menu at the pointer; repeated clicks in a focused field place
  the caret, select all, select all, and start again (the Windows rhythm).
- Look: digit keys with a 1 px edge (not in high contrast, where libadwaita draws its own borders:
  the window carries the style class `hc-high-contrast` then), operator glyphs at 18 pt, the
  `.dimmed` style class for secondary text (libadwaita 1.6+), tool tips only on keys whose name says
  more than the label, captions under the values wrap to a second line at large text sizes instead of
  being cut off (checked at 1.5 and 2 times with `HC_TEST_TEXT_SCALE`; the window minimum grows with the
  text: 377 x 514 at 1.5 times, 445 x 654 at 2 times, from 360 x 480).
- Accessibility: the result labels keep their text as their name and carry "Result in hours and
  minutes" / "Result in decimal hours" as description; `gtk_accessible_announce` tells a screen
  reader the result (or the error) when = is pressed. This could not be tried with Orca here (no
  AT-SPI bus in the headless tests): the code path is run, the announced text is checked.
- Known behaviour, not changed on purpose: (1) the converter field's caret fades at the full frame rate
  for `gtk-cursor-blink-timeout` (10 s) after each key press, as every GTK 4 text field does, and
  not at all afterwards; about 4-5 % of one core for those 10 s on a loaded VM, none on the calculator
  tab; the Windows version draws no caret animation, but the user's `gtk-cursor-blink` setting is
  not overridden. (2) On the calculator tab no widget has the focus, so Enter is "=". After Tab (or an
  arrow key) a header widget has the focus and Enter activates it, as in any GTK window (the focus ring
  shows where it is); typed digits and operators keep working. The Windows version has no focusable
  header, so Enter is always "=" there.
- Startup: measure under Xvfb (process start to first frame). GTK picks a GL renderer by default;
  measure GL vs `GSK_RENDERER=cairo` and decide with numbers (do not force a renderer without
  evidence that it is needed). The evidence exists for a machine without a usable GPU only (the
  measurements are Xvfb in WSL: software GL / Vulkan, 1.3 to 2.7 times slower to start than cairo and 48
  to 80 MiB larger), so `hourcalc-gtk` asks for cairo only when no render node (`/dev/dri/renderD*`)
  can be opened by the user or `LIBGL_ALWAYS_SOFTWARE` is set, and otherwise leaves GTK's default.
  `GSK_RENDERER` and `HOURCALC_RENDERER` (`gtk` = GTK's choice, anything else is passed on) win.
  Not measured on a machine with a GPU: do it before changing the rule.
  Measured on 2026-10-11 on the WSL VM, otherwise idle (load about 1), Xvfb, 15 runs after a warm-up, process
  start to first frame, median (min): as shipped (cairo) Debian 13 229 ms (209), Ubuntu 26.04 257 ms
  (242); GTK's own choice (software GL / Vulkan) Debian 388 ms (344), Ubuntu 893 ms (712); an empty
  libadwaita window with cairo, the toolkit floor, 175 ms (164) and 213 ms (191). On a busy VM the same
  figures are 300-600 ms for cairo (two other agents were building: 305-459 ms on Debian, 408-604 ms
  on Ubuntu), because the toolkit start dominates. HourCalc's own part is about 60 ms over the floor
  (window made 15 ms, map 31 ms, first frame 20 ms). `GTK_A11Y=none` changes nothing measurable here
  (there is no accessibility bus); memory 109 / 121 MiB with cairo against 157 / 201 MiB.
  So "well under 300 ms" holds on an idle machine for cairo and not for GTK's own renderer here; do
  not use the figure as a pass/fail line, and re-measure on real hardware.

### 3. Qt6 front end (`linux/qt`, binary `hourcalc-qt`)

- C++17, Qt6 Widgets only (no KDE Frameworks dependency, so it builds on both distros from
  qt6-base-dev). KDE look comes from the platform theme at runtime (Breeze on Plasma). Header: tab
  bar (Converter / Calculator) and a hamburger QToolButton with QMenu (Theme Auto/Light/Dark,
  Decimal places 2-6, About; plus "Keep on top" where the platform supports it: X11 only, hide it on
  Wayland). Same behaviour as above. Theme: Auto = follow the system; Light/Dark forced with
  `QStyleHints::setColorScheme` (Qt >= 6.8). About: a dialog with the GPL notice, version, copyright,
  URL and the full license text from `/usr/share/common-licenses/GPL-3`.
- **Decisions of the review of 2026-10-11:**
  - A forced theme reaches everything that is a window of its own, from the start and not only
    after the first change: the menu (`MainWindow::syncPopups`, called once the menu exists, on
    every theme change, when the menu opens and on a palette change), the context menus, the About
    and licence dialogs, and the tool tips (the tool tip palette is application wide and a palette
    that was set cannot be taken away again with Qt 6.8 / 6.10, so the tool tips are left to the
    platform until a theme is forced, and from then on carry the palette of the window, or that of
    the desktop under Auto, kept current by the palette change event).
  - No icon of the icon theme where it could be coloured for the wrong palette: the hamburger button
    always paints its three bars in the button text colour of the window's palette, and the About
    item and the Copy item of the context menu only show their theme icon while the window follows
    the desktop (Auto); a forced theme has no icons there. (An icon engine colours a symbolic icon for
    the application palette; under a forced Light or Dark opposite to the desktop that can be a dark
    glyph on a dark menu. Not seen on a real Plasma session, none is available here; the test uses an
    icon theme of its own.)
  - The label of Convert and = is white or the dark text colour of Breeze, whichever has the higher
    contrast against the fill the style really paints (the bevel is drawn into a small image and
    sampled; Fusion lightens and softens the accent, white on Fusion's version of Windows blue is only
    3 : 1), at least 4.5 : 1 on the accents tried.
  - Two legibility fixes of the acceptance check (2026-10-11), each with a measuring selftest in all six theme modes
    (Auto, Light and Dark, each on a light and on a dark desktop). (1) The address in the About dialog: Qt takes the
    link colour of rich text from the palette of the application, so a forced theme opposite to the desktop kept the
    desktop's link colour (Dark on a light desktop: rgb(0,0,255) on rgb(42,46,50), 1.59 : 1; Light on a dark desktop:
    rgb(29,153,243) on rgb(239,240,241), 2.67 : 1). The dialog now writes the link with an explicit colour taken from
    its own palette (`AboutDialog::refreshLink`, on every palette change) and moves it, hue kept, to at least 4.6 : 1
    against its window colour (`readableLink` in `util.h`; 4.64 : 1 on both Breeze windows, 7.47 : 1 for Fusion's blue
    on a light window). (2) The Keep on top check box under Fusion: the style draws its frame in a shade of the window
    colour, which is the colour of the dark menu (empty box 1.05 : 1, 1.9 : 1 in a light menu, a thin tick on a
    floating box). `PopupStyle` (a `QProxyStyle` on the menu, only when the application style is Fusion; Breeze and
    every other style are untouched) draws the check box in the menu itself from the menu palette and in proportion to
    the rectangle the style gives it: a Base fill, a frame of 4.5 to 7 : 1 against the menu (measured; the test asks
    for 3), a tick of the text colour (15 : 1 or more; the test asks for 4.5). `--render` writes `<variant>-menu-keep-on-top` (checked) and `-menu-keep-on-top-hover` next to `-menu`.
  - Ctrl+1 / Ctrl+2 also work as the number-row keys (xkb key codes 10 and 11, `digitRowTab` in
    `util.h`), so Ctrl alone with the key of 1 or 2 on a Belgian / French layout switches the tab, as
    on Windows; Qt only knows Ctrl+Shift+& as Ctrl+1 there. Ctrl+Q and Ctrl+W (and the platform's
    Quit / Close keys) close the window.
  - Keep on top (X11 only) is checked against a minimal window manager of the test's own (it maps the
    window, keeps `_NET_WM_STATE` up to date, gives the focus): toggling the item on a mapped window
    sends `_NET_WM_STATE_ABOVE` and taking it back removes it. That is the EWMH message every window
    manager acts on; KWin itself could not be tried (no desktop here). Where Qt runs on the native
    Wayland plugin the item is not offered.
  - Not changed: the tab bar, the menu button and the keypad keep `Qt::NoFocus` (SPEC 4: the keypad
    types into the field; F10 and Ctrl+1/2/Tab reach the rest), and the two metrics that depend on
    the style (`DisplayEdit::textAreaWidth` and the 2 px underline) stay as they are: no clipping
    showed in 360 combinations of font size, window size and scale on Fusion and on the Windows
    style, and Breeze is not installed here to check; replace the constant with the style's
    metrics only if a Breeze check shows a long converter value touching the edge.
- Startup, measured on 2026-10-11 on the otherwise idle WSL VM, 15 runs, process start to first paint, median
  (min): offscreen Debian 13 39.7 ms (36.0), Ubuntu 26.04 39.7 ms (35.9); the xcb platform under Xvfb (the
  acceptance figure, 66 / 65 libraries loaded against 45 / 44 offscreen) 71.7 ms (64.0) and 71.7 ms
  (64.2). (The offscreen-only figure of the first report, 62-87 ms, was taken on a busy VM.)
- Tests: `hourcalc-qt-test --selftest` (offscreen: real key and mouse events on the real widgets)
  and `--xcb-selftest` (the xcb platform on a virtual display, `linux/qt/run-headless.sh`, real key
  presses through the XTest extension: US and Belgian layouts, Ctrl+Q / Ctrl+W, Keep on top; skipped,
  exit code 77, where libXtst is not installed, as on the Debian WSL).

### 4. Packaging (`debian/`, `linux/data/`)

- One binary package `hourcalc` per distro containing both front ends
  (`/usr/libexec/hourcalc/hourcalc-gtk`, `/usr/libexec/hourcalc/hourcalc-qt`), the launcher
  `/usr/bin/hourcalc` (picks Qt when `XDG_CURRENT_DESKTOP` contains KDE, otherwise GTK;
  `HOURCALC_UI=gtk|qt`, in any case, overrides; the directory of the front ends is built into the
  script and cannot be changed from the environment, the launcher test runs a copy with that one line
  replaced), one desktop file, the AppStream metainfo, the manual page, hicolor icons
  (scalable SVG from assets\icon\app.svg plus PNGs 16-256 rendered from the 16/24 px drawings and
  app.svg) and the copyright file (DEP-5, GPL-3.0-or-later, Optimey CommV). Depends from
  `${shlibs:Depends}`, `${misc:Depends}`. Both front ends install into
  `get_option('libexecdir') / 'hourcalc'`, which `debian/rules` sets to `libexec` (debhelper's
  meson default is `lib/<multiarch>`), so the launcher, the man page and the contents check agree
  with whatever libexecdir the build uses. The package is built with `-Dtests=false` (the shipped
  binaries carry no test code); `HOURCALC_DEB_TESTS=1` (`tools\build-deb.ps1 -Tests`) builds the test
  programs too and runs the launcher test, the GTK self test (Xvfb, X11 backend) and the Qt self
  tests (offscreen, and xcb under Xvfb) in `dh_auto_test`; that needs `xvfb` and `xauth` in the build
  environment (they are not build dependencies of the default build).
- Only binary builds (`dpkg-buildpackage -b`) are supported. `debian/source/format` is
  `3.0 (quilt)`, so a source upload would need an upstream tarball (`hourcalc_1.0.0.orig.tar.xz`, made
  without `debian/`, `docs/reference/wincalc-light.png` (a screenshot that must not be distributed, see
  `debian/copyright`) and the snapshot folders of `docs/`), and for the Ubuntu revision
  `1.0.0-1~ubuntu26.04` dpkg-source warns that the version suggests Ubuntu changes without an
  `XSBC-Original-Maintainer` field. A source package built that way is lintian-clean on Debian; there is no
  script for it because nothing needs one yet.
- Application ID (desktop file name, D-Bus name, metainfo id): `be.optimey.HourCalc`
  (the owner's domain optimey.be), defined in one place per component (`app_id` in meson.build).
- Versions: `1.0.0-1~deb13` (built in Debian 13) and `1.0.0-1~ubuntu26.04` (built in Ubuntu 26.04);
  output `dist\linux\debian13\` and `dist\linux\ubuntu26.04\`.
- `lintian` clean (no errors; warnings justified or fixed), `desktop-file-validate` and
  `appstreamcli validate` clean.

## Acceptance (all measured)

1. Core: core_tests and all differential suites 0 failures/mismatches on Windows, Debian 13 and
   Ubuntu 26.04; Windows build unchanged in behaviour, not larger, not slower.
2. GTK and Qt: headless selftests (behaviour of SPEC 4-6b on the real widgets, both separators
   ',' and '.', both themes and forced themes) pass on both distros.
3. Renders (headless PNG) of both front ends in light and dark, converter and calculator states,
   menu: look native and tidy (checked by eye by a reviewer), nothing clipped, no missing glyphs.
4. Startup under Xvfb, median of 7 runs, process start to first frame: report per front end and
   distro (target: well under 300 ms; WPF was the reason to go native). The figure depends on the
   machine: measure on an idle VM with 15 or more runs, give the minimum next to the median, and
   quote it as a measurement, not as a pass/fail line (the toolkit start, `gtk_init` plus `adw_init`
   plus the application start, takes 170-400 ms of it on the WSL VM and is outside the application's
   control). For Qt the figure under the xcb platform (`--measure-startup --xcb` under
   `linux/qt/run-headless.sh`) is the acceptance figure; the offscreen one is for comparison.
5. Packages: both .debs build with dpkg-buildpackage, lintian/desktop-file-validate/appstreamcli
   clean, `apt-get install --simulate ./hourcalc_*.deb` resolves on the matching distro,
   `dpkg-deb --contents` shows the layout above. "The extracted tree runs its headless selftest"
   is two things, since the shipped binaries have no self test in them: (a) the build with
   `HOURCALC_DEB_TESTS=1` (`build-deb.ps1 -Tests`) runs the headless tests of the front ends on the
   build tree, and (b) `build-deb.sh` runs the front ends of the extracted package themselves: both
   answer `--version`, each starts headless (GTK under Xvfb, Qt offscreen), is still running after 4 s and
   writes no settings file.
6. GPL: SPDX headers on every new file, REUSE.toml updated, `debian/copyright` complete.
