# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The Qt6 Widgets front end (docs/LINUX.md section 3): hourcalc-qt, and with the 'tests' option the
# test build hourcalc-qt-test (--selftest, --render <dir>, --measure-startup). Qt6 Core, Gui and Widgets
# only (qt6-base-dev); the KDE look comes from the platform theme at run time. Included from the root
# meson.build, which defines app_id, app_version and source_url; hccore_dep comes from native/core.

qt6 = import('qt6')
qt6_dep = dependency('qt6', modules : ['Core', 'Gui', 'Widgets'], include_type : 'system')

# The application id, the version and the address of the source (defined once, in the root meson.build).
hcqt_conf = configuration_data()
hcqt_conf.set_quoted('HC_APP_ID', app_id)
hcqt_conf.set_quoted('HC_VERSION', app_version)
hcqt_conf.set_quoted('HC_SOURCE_URL', source_url)
hcqt_config = configure_file(output : 'hcqt_config.h', configuration : hcqt_conf)

# Every Q_OBJECT class lives in a header.
hcqt_moc_headers = files(
  'aboutdialog.h',
  'calculatorpage.h',
  'converterpage.h',
  'displaywidgets.h',
  'keybutton.h',
  'mainwindow.h',
)

hcqt_sources = files(
  'aboutdialog.cpp',
  'appicon.cpp',
  'calculatorpage.cpp',
  'converterpage.cpp',
  'displaywidgets.cpp',
  'keybutton.cpp',
  'mainwindow.cpp',
  'popupstyle.cpp',
  'scheme.cpp',
)

hcqt_moc = qt6.compile_moc(
  headers : hcqt_moc_headers,
  dependencies : [qt6_dep, hccore_dep])

hcqt_inc = include_directories('.')

hcqt_lib = static_library('hcqt',
  hcqt_sources, hcqt_moc, hcqt_config,
  dependencies : [qt6_dep, hccore_dep],
  include_directories : hcqt_inc)

hcqt_dep = declare_dependency(
  link_with : hcqt_lib,
  dependencies : [qt6_dep, hccore_dep],
  include_directories : hcqt_inc)

executable('hourcalc-qt',
  'main.cpp',
  dependencies : [hcqt_dep],
  install : true,
  install_dir : get_option('libexecdir') / 'hourcalc')

if get_option('tests')
  qt6_test_dep = dependency('qt6', modules : ['Test'], include_type : 'system')
  # The real-input test talks to the X server of a virtual display (Xlib; XTest is loaded at run time).
  x11_dep = dependency('x11', required : false)
  hcqt_test_sources = ['testmain.cpp', 'selftest.cpp', 'render.cpp', 'startup.cpp', 'xcbtest.cpp']
  hcqt_test_args = []
  if x11_dep.found()
    hcqt_test_sources += ['x11helper.cpp']
    hcqt_test_args += ['-DHC_TEST_X11']
  endif
  hcqt_test = executable('hourcalc-qt-test',
    hcqt_test_sources,
    cpp_args : hcqt_test_args,
    dependencies : [hcqt_dep, qt6_test_dep, x11_dep],
    install : false)

  # Headless: the program forces QT_QPA_PLATFORM=offscreen itself, so no window can reach a desktop.
  test('qt-selftest', hcqt_test, args : ['--selftest'], suite : 'qt', timeout : 180)

  # The real platform plugin on a virtual display (the program refuses to start anywhere else). Skipped (exit code 77)
  # where libXtst is not installed; not registered where Xvfb or the X11 library is missing.
  xvfb_run = find_program('xvfb-run', required : false)
  if xvfb_run.found() and x11_dep.found()
    test('qt-xcb-selftest', find_program('sh'),
      args : [files('run-headless.sh'), hcqt_test, '--xcb-selftest'],
      suite : 'qt',
      timeout : 180)
  endif
endif
