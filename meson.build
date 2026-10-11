# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HourCalc Linux build (docs/LINUX.md). Each part lives in its own subdirectory and is only
# included when its meson.build exists, so the parts can be developed independently:
#   native/core  - shared core: libhccore, hc_cli, core_tests
#   linux/gtk    - GTK4 + libadwaita front end (hourcalc-gtk)
#   linux/qt     - Qt6 Widgets front end (hourcalc-qt)
#   linux/data   - launcher, desktop file, metainfo, icons
#   linux/tests  - headless test drivers

project('hourcalc', 'cpp',
  version : '1.0.0',
  license : 'GPL-3.0-or-later',
  meson_version : '>= 1.0.0',
  default_options : ['cpp_std=c++17', 'warning_level=3', 'buildtype=release', 'b_ndebug=if-release'])

fs = import('fs')

# Placeholder until the owner confirms the repository / domain (docs/LINUX.md, section 4).
app_id = 'io.github.Optimey_CommV.HourCalc'
app_version = meson.project_version()
source_url = 'https://github.com/Optimey-CommV/hourcalc'

if fs.is_file('native/core/meson.build')
  subdir('native/core')
endif
if get_option('gtk') and fs.is_file('linux/gtk/meson.build')
  subdir('linux/gtk')
endif
if get_option('qt') and fs.is_file('linux/qt/meson.build')
  subdir('linux/qt')
endif
if fs.is_file('linux/data/meson.build')
  subdir('linux/data')
endif
if get_option('tests') and fs.is_file('linux/tests/meson.build')
  subdir('linux/tests')
endif
