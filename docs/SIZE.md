# HourCalc: where the bytes go

> **Note for the public repository.** This is a working document of the development. Material it
> mentions that was only working material is not published: the earlier WPF build (`legacy\wpf\`),
> its snapshots (`docs\snapshots\`), the snapshot renders and comparisons, the logo proposals
> (`logos\`) and a reference screenshot of Microsoft's Calculator.

Measured on 2026-10-10 with MSVC 14.44.35207 and Windows SDK 10.0.26100 (x64, the hybrid CRT: static
vcruntime, system UCRT). Sizes are bytes; "KB" is 1024 bytes like the build output. A PE file is padded to
its FileAlignment of 512, so the file size moves in steps of 512 and small savings only show in the section
sizes. Both are given where they differ.

## Result

| build | before | after | saved |
|---|---:|---:|---:|
| `dist\HourCalc.exe`, source zip embedded (the default) | 353,792 | 309,760 | 44,032 (-12.4%) |
| `build.ps1 -NoEmbeddedSource` (opt-in) | - | 196,096 | 157,696 (-44.6%) |

**Update, theme choice (SPEC 6b, same day):** the Theme section of the menu, the theme mode, the combined
settings file and the re-theming of open popups and dialogs make the default build (without the embedded
zip) 198,656 bytes (194.0 KB), 2,560 bytes more than the 196,096 below, still under 200 KB whether that is counted as
204,800 or as 200,000 bytes (the tables below are the measurement before this change).

**Update, review of the theme choice (2026-10-10, later):** the settings file moved to `%LOCALAPPDATA%\HourCalc\` and
is saved with a re-read of the other setting, as a temporary file that is renamed over it (a reader never sees half
a file; with a retry when a reader or a virus scanner holds the old file for a moment), and a click inside the menu
box on something that is not an item no longer closes it. The default build is now 199,168 bytes (194.5 KB), 512 bytes
more than the 198,656 above, still under 200 KB whether that is counted as 204,800 or as 200,000 bytes. What it took
to stay under 200,000, measured one change at a time by building (the file moves in steps of 512, so a change only
shows when it moves a section over a step; sizes of `.text` and `.rdata` are the virtual sizes):

| change | `.text` | `.rdata` | file |
|---|---:|---:|---:|
| first version: shell known folder (`SHGetKnownFolderPath`, shell32 and ole32 delay-loaded), `FlushFileBuffers`, name of the temporary file from process id and tick count | 0x2056C | 0x9300 | 200,704 |
| write through instead of `FlushFileBuffers`, name of the temporary file from the process id only (`CREATE_ALWAYS`) | 0x204DC | 0x92B2 | 200,192 |
| one loop for the little and the big endian forms of UTF-16 and UTF-32 in the settings decoder | 0x2048C | 0x92B2 | 200,192 |
| the folder from the environment variable `LOCALAPPDATA` only (no registry value, no shell); the name of the temporary file from four letters made of the process id instead of its decimal text | 0x2042C | 0x921A | 200,192 |
| no `GetFileAttributesW` check in front of the save (a read-only file or a folder fails after the retries), `rfind` of one separator | 0x2033C | 0x91DC | 199,168 |

(SHGetKnownFolderPath was also measured on the owner's PC: the first call of a process, with gdiplus and dwrite loaded
already, took 22 ms and loaded thirteen modules, one of them a third-party shell extension, so it is not used for the
start-up path.) The `-EmbedSource` variant was built once with these changes as well: 321,536 bytes (314.0 KB), its
self test 2,158 checks, 0 failed; the tables below are not remeasured for either variant.

**Update, portable Dec (2026-10-10, Linux work):** `hc::Dec` is own code now (`native\core\dec.cpp`, System.Decimal on
32-bit limbs, the same file on Windows and Linux) instead of a wrapper around oleaut32. The exe no longer imports
`OLEAUT32.dll`, and the default build (no embedded source) delay-loads nothing any more, so the delay-load helper
(about 2 KB of code) and its tables are gone as well (`build-app.ps1` links `delayimp.lib` only for the variant that
delay-loads comdlg32). The default build is now 198,656 bytes (194.0 KB), 512 bytes less than the 199,168 above:
`.text` 0x2033C -> 0x2037C (+64: the own arithmetic is about as big as the old wrapper plus the helper that went),
`.rdata` 0x91DC -> 0x8ED0 (-780), `.data` 0xF10 -> 0xE90 (-128), `.reloc` 0x1C4 -> 0x1B8 (-12); the file moves in steps of
512. The platform split (`platform_win.cpp`) costs nothing measurable. Hidden start-up (`HourCalc-test-noembed.exe
--measure-startup --hidden`, old and new exe alternating, 3 rounds of 7 runs, machine not idle): median 194 ms before,
197 ms after, mean 221 and 200 ms, CPU time 109 ms both, i.e. no difference beyond the noise; the start-up path never ran
the arithmetic (the idle display is written out), and the import of one DLL less is below what can be measured here.

**Update, wide-divisor rounding and a longer save retry (2026-10-11, review of the Linux version):** `dec.cpp` copies the
runtime's rounding of a quotient whose divisor has more than 64 bits (`RoundsUpWide`, about 100 bytes of code), and the
rename of a settings save is retried for up to 500 ms instead of 160 ms (`platform_win.cpp`, one constant). The default build
is 199,168 bytes (194.5 KB) again, 512 bytes more than the 198,656 above (the file moves in steps of 512) and the same as
before the portable Dec; still under 200 KB whether that is counted as 204,800 or as 200,000 bytes. Hidden start-up (test exe
of the default build, both exes on the local disk, alternating, 3 rounds of 7 runs, machine idle, twice): median 85 and 86 ms
before, 90 and 80 ms after, minimum 76 / 76 against 80 / 71: no difference beyond the noise (from a network share the same exe
takes 208 ms, so measure from a local disk).

The target of NATIVE.md (<= 200 KB) cannot be met with the source zip inside: the zip alone is
123,189 bytes. Without it the exe is 196,096 bytes (191.5 KB), under 200 KB whether that is counted as
204,800 or as 200,000 bytes. Both numbers are for the unsigned exe; an Authenticode signature with its
timestamp adds several KB (not measured here: signing is not done in this work).

| | before | after (default) | after (-NoEmbeddedSource) |
|---|---:|---:|---:|
| file | 353,792 | 309,760 | 196,096 |
| headers | 1,024 | 1,024 | 1,024 |
| .text | 149,468 | 131,084 | 129,436 |
| .rdata | 42,278 | 37,146 | 36,528 |
| .data (initialized + bss) | 4,240 | 3,856 | 3,856 |
| .pdata | 5,580 | merged into .rdata | merged into .rdata |
| .rsrc | 152,800 | 137,992 | 26,920 |
| .reloc | 508 | 452 | 452 |

## How it was measured

- Link with `/MAP:hc.map`, then `python tools\mapsize.py hc.map HourCalc.exe`. The map lists every public and
  static symbol with its address and the object it came from, also under `/LTCG` (the object of a function
  that was folded into another one keeps the first). The size of a symbol is the distance to the next symbol
  of the segment; folded copies cost nothing. The `$unwind$`, `$cppxdata$`, `$ip2state$` symbols give the
  unwind information per object; `.pdata` is counted from the exception directory of the exe (12 bytes per
  function). `dbghelp`'s `UnDecorateSymbolName` told the STL instantiations from the own code.
- Every lever was tried on its own against the build before it, with the same sources, by compiling all of
  `native\app` and `native\core` with the flags and linking with the same libraries as `build-app.ps1`
  (hybrid CRT, delay-loaded `oleaut32` and `comdlg32`). `.text`, `.rdata`, `.pdata`, `.data` and `.reloc` are
  the virtual sizes of the linked image.
- The final exes are checked the same way: the objects of the real build, relinked with `/MAP`, are byte for
  byte the same size as `dist\HourCalc.exe`.

## Before: where the 353,792 bytes were

The baseline is the build of 2026-10-10 06:04 (`/O1 /GL /Gy /GS`, hybrid CRT, `/LTCG /OPT:REF /OPT:ICF`).

### .text, 149,468 bytes

| group | bytes |
|---|---:|
| core library (hc_core.lib) | 28,520 |
| application app_layout.obj | 17,594 |
| application app_window.obj | 16,667 |
| vcruntime (EH, memcpy, ...) | 16,563 |
| application dialogs.obj | 14,600 |
| application app_logic.obj | 13,620 |
| application flyout.obj | 11,052 |
| application gfx.obj | 7,276 |
| application embedded.obj | 4,888 |
| static CRT startup (LIBCMT) | 4,841 |
| application app_paint.obj | 4,740 |
| C++ library (libcpmt) | 4,608 |
| delay-load helper | 2,074 |
| application theme.obj | 776 |
| application winmain.obj | 516 |
| application clipboard.obj | 368 |
| LTCG generated (CIL) | 236 |
| import thunks (oleaut32) | 205 |
| UCRT import thunks | 191 |
| import thunks (comdlg32) | 133 |

| object | bytes |
|---|---:|
| app_layout.obj | 17,594 |
| app_window.obj | 16,667 |
| dialogs.obj | 14,600 |
| app_logic.obj | 13,620 |
| flyout.obj | 11,052 |
| hc_core:calcengine.obj | 8,197 |
| libvcruntime:frame.obj | 8,065 |
| gfx.obj | 7,276 |
| hc_core:timemath.obj | 6,941 |
| hc_core:settings.obj | 5,314 |
| embedded.obj | 4,888 |
| hc_core:converter.obj | 4,784 |
| app_paint.obj | 4,740 |
| libcpmt:vector_algorithms.obj | 4,176 |
| hc_core:dec.obj | 3,284 |
| libvcruntime:risctrnsctrl.obj | 2,257 |
| (61 more, each smaller) | 16,013 |

| item (in the .text of the baseline) | bytes |
|---|---:|
| inflate (RFC 1951 decoder in embedded.cpp) | 604 |
| zip directory reading (ZipRead, ZipNames) | 776 |
| blur and shadow of the popups (GaussBlur, RoundRectCoverage, BuildPixels) | 2,652 |
| main window layout (Relayout, BuildKeys, StarSizes, Boundaries, TieOrder, Fit*) | 10,606 |
| C++ exception runtime of vcruntime (frame, risctrnsctrl, riscchandler, ehhelpers, ehstate, handlers) | 12,191 |
| memcpy, memset, memcmp of vcruntime | 2,864 |
| STL instantiations in the application and core objects: std::vector | 8,716 |
| ... std::basic_string | 5,610 |
| ... the rest (exception classes, fill, pair, allocate) | 2,492 |

The application (`native\app`) is 77 KB and the core 27 KB. Of the 104 KB of own code about 17 KB are STL
template instantiations (vector, basic_string) that the compiler did not inline; they are spread over every
object. The decoder in `embedded.cpp` is small (about 0.6 KB for inflate); the 4.2 KB of `libcpmt` is the
vectorized STL algorithms (see below).

### .rdata, 42,278 bytes (constants, unwind information, import tables)

| group | bytes |
|---|---:|
| C++ library (libcpmt) | 10,864 |
| vcruntime (EH, memcpy, ...) | 5,500 |
| core library (hc_core.lib) | 3,240 |
| import thunks (gdiplus) | 2,530 |
| application app_layout.obj | 2,352 |
| application gfx.obj | 2,348 |
| application dialogs.obj | 1,880 |
| UCRT import thunks | 1,802 |
| linker | 1,324 |
| application app_window.obj | 1,316 |
| application embedded.obj | 1,296 |
| import thunks (dwrite) | 1,090 |
| (16 more, each smaller) | 6,736 |

| kind of .rdata symbol | bytes |
|---|---:|
| vectorized STL algorithms: lookup tables | 9,624 |
| import tables (names, thunks, addresses) | 7,474 |
| other constants and tables | 7,240 |
| x64 unwind information | 7,012 |
| string literals | 4,096 |
| undname.obj tables (pulled in by type_info) | 3,544 |
| C++ exception tables | 2,976 |
| vtables | 312 |

- 10 KB (`libcpmt:vector_algorithms.obj`) were lookup tables of the vectorized STL algorithms (`_Remove_tables_*`,
  `_Tail_masks`) plus 4.2 KB of code in `.text`, linked in because `std::wstring::find_first_of`,
  `find_last_of`, `std::find` and `std::search` on `wchar_t` call `__std_find_first_of_trivial_pos_2`,
  `__std_find_last_of_trivial_pos_2`, `__std_find_trivial_2` and `__std_search_2`. The tables are in one
  non-COMDAT section of that object, so `/OPT:REF` cannot drop the unused ones.
- 3.5 KB (`libvcruntime:undname.obj`) are the name tables of `__unDName`: the code is gone after `/OPT:REF`,
  but the object is linked in through the `type_info` vftable that every exception type descriptor points to,
  and its tables are in non-COMDAT sections.
- The import tables are about 7.5 KB with the import address table: 17 DLLs (`api-ms-win-crt-*` of the UCRT,
  `kernel32`, `user32`, `gdi32`, `gdiplus`, `dwrite`, ...), names and thunks of 208 functions.

### .pdata, 5,580 bytes

465 entries of 12 bytes: the core 84, vcruntime 77 (the EH runtime), the C runtime startup 46, the application
files 28 to 48 each. One entry per function that has a stack frame or an unwind handler.

### .data, 4,240 bytes (1,536 on disk, the rest is bss)

`gs_report.obj` (1.4 KB of bss for the `/GS` failure report), the GDI+ and DirectWrite state of `gfx.obj`
(1.6 KB), the rest is small.

### .rsrc, 152,800 bytes

| resource | bytes |
|---|---:|
| icon frames (RT_ICON) | 21,974 |
| RCDATA 101: the source zip | 127,781 |
| icon directory (RT_GROUP_ICON) | 118 |
| version information | 776 |
| manifest | 1,406 |
| directory tables and padding | 745 |
| .rsrc section | 152,800 |

The source zip, 127,781 bytes, holds:

| entry | size | stored |
|---|---:|---:|
| assets/icon/app.ico | 22,108 | 13,399 |
| LICENSE | 35,149 | 12,112 |
| native/app/dialogs.cpp | 22,892 | 7,080 |
| native/app/app_window.cpp | 22,238 | 6,620 |
| native/app/gfx.cpp | 20,255 | 6,183 |
| native/app/flyout.cpp | 19,483 | 5,956 |
| native/cli/hc_cli.cpp | 17,282 | 4,997 |
| native/app/app_layout.cpp | 15,452 | 4,797 |
| README.md | 10,470 | 4,687 |
| native/app/app_logic.cpp | 16,808 | 4,596 |
| native/app/app.h | 12,865 | 4,547 |
| native/app/embedded.cpp | 11,532 | 3,470 |
| (31 more entries) | 121,360 | 44,371 |
| total, 43 entries | 347,894 | 122,815 |

The icon was stored twice, as the exe's own icon (22 KB) and inside the zip (13 KB, the BMP frames deflate
well, the PNG frames not).

## Levers

Each row was measured on its own (compile, link, compare the section sizes) against the build with
`/D_USE_STD_VECTOR_ALGORITHMS=0`, the first row against the baseline, so the rows do not add up exactly (`/Ob0`
saves less after `/Oi`: -4,544 instead of -6,956). "Sections" is the change of `.text + .rdata + .pdata +
.data + .reloc`, "file" the change of the file size (512 byte steps). Tests: the core, oracle and differential
tests, the self test (1,718 checks of the default build) and the snapshot render all pass with the levers marked
applied; the renders are byte for byte identical to the ones of the baseline. All compiler and linker levers
took the baseline exe (still with its old resources) from 353,792 to 329,728 bytes with `/Ob0` everywhere, and
with the inlining refined to about 324,600; the icon, the zip and the manifest are the rest.

| lever | sections | file | applied |
|---|---:|---:|---|
| `/D_USE_STD_VECTOR_ALGORITHMS=0` (the documented switch for the vectorized STL algorithms; the scalar versions give the same results) | -13,452 | -12,800 | yes: removes `libcpmt:vector_algorithms.obj` |
| `/Ob0` instead of the default `/Ob2` of `/O1`, everywhere (no inlining: 11 KB less code, 4 KB more `.pdata`) | -6,956 | -7,680 | refined by the next two rows |
| `/Ob1` | +1,364 | +1,024 | no, bigger |
| core library back to `/Ob2` (each of its five files alone with `/Ob0` is 144 to 656 bytes bigger: the string handling is smaller inlined) | -1,576 | -2,048 | yes |
| per file for `native\app`: `/Ob0` only for `app_layout.cpp` and `app_window.cpp`, the others `/Ob2` (see below) | -3,960 | -3,584 | yes |
| `/Oi` with `/O1` (intrinsics; the same as `/O2 /Os`) | -3,620 | -4,096 | yes |
| `/GR-` (nothing uses RTTI: no `dynamic_cast`, no `typeid`) | -1,624 | -1,536 | yes |
| `/Gw` (data in COMDATs for `/OPT:REF`) | -164 | 0 | yes |
| link `/NOCOFFGRPINFO` (the COFF group table of `/LTCG` in the debug directory, read only by profilers) | -1,104 | -1,024 | yes |
| link `/MERGE:.pdata=.rdata` (both read-only: one section less to pad) | -84 | -512 | yes |
| icon: all frames PNG (RGBA, rows unfiltered, best zlib strategy of six) instead of BMP frames for 16 to 32 px | -9,940 in `.rsrc` | -9,940, and -1,755 stored in the zip | yes |
| source zip: `native\cli\hc_cli.cpp` left out (`build-core.ps1 -SkipTests` builds only the library) | -5,121 in the zip | | yes |
| source zip: the test section moved from `build-core.ps1` to `tests\core-tests.ps1` | -945 in the zip | | yes |
| manifest without comments and indentation (`build-app.ps1` makes the copy that is embedded) | -289 in `.rsrc` | -512 in the variant | yes |
| `/Zc:inline`, `/Zc:threadSafeInit-`, `/Oi-`, link `/OPT:ICF=5` | 0 | 0 | no effect |
| `/Zc:sizedDealloc-` | -28 | -512 at the boundary | no, 28 bytes of real change |
| `/Zc:throwingNew` | 0 | 0 | no effect |
| `/D_HAS_EXCEPTIONS=0` (the STL throws nothing; our own `throw` is unaffected) | +420 | +1,024 | no, bigger |
| link `/Brepro` (reproducible exe, adds a debug directory entry) | +32 | +512 at the boundary | no |
| link `/MERGE:.reloc=...` | - | - | no, `LNK1272: cannot merge '.reloc'` |
| link `/MERGE:.rdata=.text /MERGE:.pdata=.text` (all in one RX section) | +77 | +512 | no: no gain (the imports stay in `.idata`), data in an executable section |
| link `/EMITTOOLVERSIONINFO:NO`, `/STUB:<64 byte stub>`, `/NOVCFEATURE` | 0 | 0 | no: the headers stay 1,024 bytes, five sections need 528 bytes even without the Rich header and the DOS stub (the limit is 512) |
| palette PNG frames (8 bit, tRNS) instead of RGBA | +500 | | no, bigger (the artwork has 17 to 524 colours, the palette costs more than it saves) |
| 7-Zip `-mx9 -mpass15` for the zip instead of .NET `Optimal` | -742 in the zip | | no: .NET already is at zlib level 9 (122,815 against 122,756 for the same files) and the build would depend on 7-Zip |
| static CRT instead of the hybrid CRT (kept by `build-app.ps1`, which builds both and keeps the smaller) | +109,568 | | no |

Inlining per file. The numbers are the sum of the section sizes of the `-NoEmbeddedSource` exe with the core
at `/Ob2` and every file of `native\app` at `/Ob0` (174,232 bytes, file 199,680), one file at a time switched to
`/Ob2`: `app_layout` +9,336, `app_logic` +160, `app_window` +552, `flyout` 0, `app_paint` -296, `clipboard`
-160, `dialogs` -568, `embedded` -640, `gfx` -2,544, `theme` -64, `winmain` -32. Combinations (sections, file):
`app_layout` + `app_window` at `/Ob0`, the rest at `/Ob2`: 170,272 and 196,096 (chosen; `flyout` at `/Ob0`
too: 170,256); `app_layout`, `app_logic`, `app_window`: 170,336; `app_layout` + `app_logic`: 170,976;
`app_layout` alone: 171,032; with `app_paint` as well: 170,520; the chosen set plus `embedded`, `dialogs` or
`gfx` at `/Ob0`: 171,320, 170,808, 172,776. `build-app.ps1` has the list (`$noInline`); `tools\cflags.rsp` has
no `/Ob` switch, so the core, the console front end and the tests use the `/Ob2` of `/O1`.

Timing note. `--selftest` with `HC_TRACE=1` prints the cost of the first Advanced-menu layout (DirectWrite text
layout, blur, pixels) and of the first window (created hidden, laid out, painted twice, in a cold process).
First menu layout, medians of 9 interleaved runs: 8.8 ms for the baseline build, 7.5 ms with `/Ob2` everywhere,
13.0 ms with `/Ob0` on every file and 13.3 ms with `/Ob0` on `app_layout` and `app_window` only, so showing the
menu for the first time costs about 4.5 ms more. First paint of the main window (layout included), medians
of 15 runs: 5.3 ms for the baseline, 4.9 ms with `/Ob2`, 5.1 ms for the chosen set; minimum 2.0, 2.1 and 2.1 ms:
no change. The CPU time of `--render` (34 snapshots, popups included) is the same within the noise (minimum
484 ms against 500 ms). I could not run `--measure-startup` (not allowed), so the effect on the 160 ms start is
not measured; if `tools\starttime.ps1` shows more than a few ms, empty `$noInline` in `build-app.ps1` (the exe
grows by about 4 KB).

## After: where the bytes are

### .text of the default exe, 131,084 bytes

| group | bytes |
|---|---:|
| core library (hc_core.lib) | 20,806 |
| application app_layout.obj | 20,598 |
| vcruntime (EH, memcpy, ...) | 16,411 |
| application app_window.obj | 14,363 |
| application dialogs.obj | 12,560 |
| application flyout.obj | 10,428 |
| application app_logic.obj | 9,720 |
| application gfx.obj | 7,292 |
| static CRT startup (LIBCMT) | 4,826 |
| application app_paint.obj | 4,544 |
| application embedded.obj | 4,528 |
| delay-load helper | 2,076 |
| application theme.obj | 732 |
| application winmain.obj | 512 |
| C++ library (libcpmt) | 432 |
| application clipboard.obj | 384 |
| LTCG generated (CIL) | 324 |
| UCRT import thunks | 208 |
| import thunks (oleaut32) | 207 |
| import thunks (comdlg32) | 133 |

### .rdata of the default exe, 37,146 bytes (the exception directory is merged into it)

| group | bytes |
|---|---:|
| exception tables (.pdata) | 7,352 |
| vcruntime (EH, memcpy, ...) | 5,552 |
| core library (hc_core.lib) | 3,108 |
| import thunks (gdiplus) | 2,494 |
| application app_layout.obj | 2,000 |
| application dialogs.obj | 1,840 |
| UCRT import thunks | 1,750 |
| application app_window.obj | 1,316 |
| application embedded.obj | 1,312 |
| static CRT startup (LIBCMT) | 1,188 |
| application gfx.obj | 1,188 |
| import thunks (dwrite) | 1,090 |
| (17 more, each smaller) | 6,956 |

| kind of .rdata symbol | bytes |
|---|---:|
| .pdata (exception directory, merged here) | 7,352 |
| import tables (names, thunks, addresses) | 7,334 |
| x64 unwind information | 6,580 |
| other constants and tables | 5,100 |
| string literals | 4,112 |
| undname.obj tables (pulled in by type_info) | 3,544 |
| C++ exception tables | 2,860 |
| vtables | 264 |

### .rsrc of the default exe, 137,992 bytes

| resource | bytes |
|---|---:|
| icon frames (RT_ICON) | 12,034 |
| RCDATA 101: the source zip | 123,189 |
| icon directory (RT_GROUP_ICON) | 118 |
| version information | 776 |
| manifest | 1,117 |
| directory tables and padding | 758 |
| .rsrc section | 137,992 |

The source zip is 123,189 bytes now:

| entry | size | stored |
|---|---:|---:|
| LICENSE | 35,149 | 12,112 |
| assets/icon/app.ico | 12,168 | 11,575 |
| native/app/dialogs.cpp | 23,361 | 7,195 |
| native/app/app_window.cpp | 22,238 | 6,620 |
| native/app/gfx.cpp | 20,255 | 6,183 |
| native/app/flyout.cpp | 19,483 | 5,956 |
| README.md | 11,219 | 4,961 |
| native/app/app_layout.cpp | 15,452 | 4,797 |
| build-app.ps1 | 13,211 | 4,706 |
| native/app/app_logic.cpp | 16,808 | 4,596 |
| native/app/app.h | 12,865 | 4,547 |
| native/app/embedded.cpp | 12,490 | 3,745 |
| (30 more entries) | 112,449 | 41,348 |
| total, 42 entries | 327,148 | 118,341 |

### The `-NoEmbeddedSource` exe, 196,096 bytes

| group | bytes |
|---|---:|
| core library (hc_core.lib) | 20,806 |
| application app_layout.obj | 20,598 |
| vcruntime (EH, memcpy, ...) | 16,415 |
| application app_window.obj | 14,363 |
| application dialogs.obj | 12,104 |
| application flyout.obj | 10,428 |
| application app_logic.obj | 9,720 |
| application gfx.obj | 7,292 |
| static CRT startup (LIBCMT) | 4,842 |
| application app_paint.obj | 4,544 |
| application embedded.obj | 3,440 |
| delay-load helper | 2,076 |
| (7 more, each smaller) | 2,808 |

| resource | bytes |
|---|---:|
| icon frames (RT_ICON) | 12,034 |
| RCDATA 102: LICENSE, deflated | 12,116 |
| icon directory (RT_GROUP_ICON) | 118 |
| version information | 776 |
| manifest | 1,117 |
| directory tables and padding | 759 |
| .rsrc section | 26,920 |

In this variant `resource 101` (the zip) is gone, `resource 102` is `LICENSE` as a raw deflate stream behind its
length (12,116 bytes), and `comdlg32` is not imported (no Save dialog). The About window has no *Save source
code...* button; a line under the notice reads `Source code: https://github.com/Optimey-CommV/hourcalc` (a
placeholder, `embedded::kSourceUrl`). Everything else, including the snapshots of the main window, the
menus and the license viewer, is byte for byte the same as in the default build; only `light-about.png` and
`dark-about.png` differ.

## Not applied, with what it would bring

| idea | estimate | why not |
|---|---:|---|
| No C++ exceptions at all: error codes instead of `DecOverflow`, `DecFormat`, `DecDivideByZero` | 18 to 22 KB (vcruntime EH 12.6 KB of `.text`, the EH tables, `libcpmt:xthrow`, the `type_info` and `undname` tables) | the exceptions are the contract between the core and the differential tests (`hc_core.h`, `hc_cli.cpp`, `core_tests.cpp`); `new` still throws `bad_alloc` |
| Own `memcpy`, `memset`, `memmove` with `__movsb`/`__stosb` (`memcpy.obj` defines both `memcpy` and `memmove`) | about 2.6 KB (the vcruntime objects) | an override of CRT internals, slower for large blocks; not tried |
| Define the `type_info` vftable ourselves so that `std_type_info_static.obj` and `undname.obj` are not linked | about 3.5 KB (`undname.obj` data) | relies on CRT internals; not tried |
| Load `oleaut32` by hand (`LoadLibrary`/`GetProcAddress` for the ten `VarDec*` calls) instead of `delayimp` | about 2.3 KB (the delay-load helper) | obsolete: the core has its own `hc::Dec` now, no oleaut32 at all (see the update at the top) |

The estimates are read from the map; none of the three was built.

## Verification

Run on the final tree, the full build from a console with input code page 850 (see the end of this file):

| check | result |
|---|---|
| `core_tests.exe` (C++ unit tests of the core) | 1,269 checks, 0 failed |
| C# tests of the reference (`tests\Tests.cs`) | 1,174 checks, 0 failed |
| `tests\Differential.cs`, native core against the C# reference | 23,930,302 checks, 0 mismatches |
| `tests\DifferentialReview.cs` (default sample, `--scale=0.05`) | 10,507,011 checks, 0 mismatches (35,018 more in the info suites, 7,086 of them a documented known limit) |
| the same with the private state of the calculator (`review_state_cli.exe`) | 453,078 checks, 0 mismatches |
| `HourCalc-test.exe --selftest`, default build | 1,718 checks, 0 failed (100 more than before: the icon frames and the archive contents) |
| `HourCalc-test-noembed.exe --selftest`, `-NoEmbeddedSource` | 1,666 checks, 0 failed (the archive checks replaced by checks of the license resource and the About window) |
| `--render` into `docs\snapshots-native`, the 34 snapshots | byte for byte identical to the renders of the baseline build |
| `tools\compare_snapshots.py` against the WPF snapshots | before and after the same: 34 images, mean of >16% 0.35, worst >16% 3.24 (`light-converter-150dpi`), worst >64% 1.61 (`light-converter-150dpi`) |
| the exe built from its own source zip (`obj\zipcheck3`, `build.ps1` there) | the same size, 309,760 bytes; the zip it builds is byte for byte the one it came from; with `-NoEmbeddedSource` 196,096 bytes |
| icon frames, old and new file, through `LoadImageW` (12 sizes from 16 to 256) | the same pixels; every frame also loads through `CreateIconFromResourceEx` |

After the portable Dec (full `build.ps1`, 2026-10-10): `core_tests.exe` 1,577 checks, C# tests of the reference 1,402,
`tests\Differential.cs` 24,082,067 checks, `tests\DifferentialReview.cs` (default sample) 14,049,356 checks plus 35,018 in
the info suites, with the private state 453,078, `--selftest` 2,106 checks: all 0 failed / 0 mismatches. The "known limit"
of the division (7,086 cases of the older run) is gone: the own division is exact there, so the review test no longer
excuses anything.

After the wide-divisor rounding and the longer save retry (full `build.ps1`, 2026-10-11): `core_tests.exe` 1,739 checks, C# tests
of the reference 1,402, `tests\Differential.cs` 24,282,067 checks (the new suite of near-half divisions with a divisor of more
than 64 bits is 200,000 of them), `tests\DifferentialReview.cs` 14,049,356 checks plus 35,018 in the info suites, with the private
state 453,078, `--selftest` 2,106 checks: all 0 failed / 0 mismatches.

## Reproducing

```
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1                      # default exe, all tests
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -NoEmbeddedSource    # the variant
```

For a map of a build, link its objects (`obj\app\release\*.obj`, `obj\app\app.res`, `obj\core\hc_core.lib`) the way
`build-app.ps1` does and add `/MAP:hc.map`, then `python tools\mapsize.py hc.map dist\HourCalc.exe`.

A side finding. The differential tests (`tests\Differential.cs`, `tests\DifferentialReview.cs`) write their requests
to `hc_cli.exe` and `review_state_cli.exe` through `Process.StandardInput.BaseStream`. When the console input code
page is 65001 (the PowerShell tool of Claude Code has it), .NET writes a UTF-8 byte order mark into that stream
first, the first request answers `!UNKNOWN` and the test stops with "hc_cli.exe does not answer ping". It
stopped a full build run from that tool. Both console programs now skip a byte order mark at
the start of their input (`bomChecked` in `native\cli\hc_cli.cpp` and `tests\native\review_state_cli.cpp`); the
quick differential runs pass from a code page 65001 console and from one with code page 850.
