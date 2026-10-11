# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The portable core (docs/LINUX.md, section 1): libhccore, and with -Dtests=true the console front end hc_cli of the
# differential tests, the C++ unit tests core_tests and review_state_cli (the calculator state for the review test).
# The Windows build does not use this file (build-core.ps1 compiles the same sources with MSVC and
# platform_win.cpp instead of platform_posix.cpp).

core_inc = include_directories('.')

hccore_sources = files(
  'calcengine.cpp',
  'converter.cpp',
  'dec.cpp',
  'platform_posix.cpp',
  'settings.cpp',
  'timemath.cpp',
)

hccore = static_library('hccore', hccore_sources,
  include_directories : core_inc,
  install : false)

# What the front ends link: the library and the include directory of hc_core.h.
hccore_dep = declare_dependency(link_with : hccore, include_directories : core_inc)

if get_option('tests')
  threads_dep = dependency('threads')

  if fs.is_file('../cli/hc_cli.cpp')
    hc_cli_exe = executable('hc_cli', '../cli/hc_cli.cpp',
      dependencies : hccore_dep,
      install : false)
  endif

  if fs.is_file('../../tests/native/core_tests.cpp')
    core_tests_exe = executable('core_tests', '../../tests/native/core_tests.cpp',
      dependencies : [hccore_dep, threads_dep],
      install : false)
    test('core_tests', core_tests_exe, timeout : 300)
  endif

  if fs.is_file('../../tests/native/review_state_cli.cpp')
    review_state_cli_exe = executable('review_state_cli', '../../tests/native/review_state_cli.cpp',
      dependencies : hccore_dep,
      install : false)
  endif
endif
