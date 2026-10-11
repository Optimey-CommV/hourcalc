# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The GTK4 + libadwaita front end (docs/LINUX.md section 2): hourcalc-gtk, and with the 'tests' option the
# test build hourcalc-gtk-test (--selftest, --render <dir>, --measure-startup; it never starts the normal GUI).
# C++17 on the GTK / libadwaita C API (no gtkmm). Included from the root meson.build, which defines app_id,
# app_version and source_url; hccore_dep comes from native/core.
#
# The code builds and runs on the oldest target, Debian 13 with GTK 4.18 and libadwaita 1.7 (the inline view
# switcher of the header needs 1.7); newer API only behind version checks.

gtk4_dep = dependency('gtk4', version : '>= 4.14')
adw_dep = dependency('libadwaita-1', version : '>= 1.7')

# The application id, the version and the address of the source (defined once, in the root meson.build).
hcgtk_conf = configuration_data()
hcgtk_conf.set_quoted('HC_APP_ID', app_id)
hcgtk_conf.set_quoted('HC_VERSION', app_version)
hcgtk_conf.set_quoted('HC_SOURCE_URL', source_url)
hcgtk_config = configure_file(output : 'hcgtk_config.h', configuration : hcgtk_conf)

hcgtk_sources = files(
  'app.cpp',
  'hcwindow.cpp',
  'hcwindow_build.cpp',
  'hcwindow_fit.cpp',
  'util.cpp',
)

hcgtk_inc = include_directories('.')

hcgtk_lib = static_library('hcgtk',
  hcgtk_sources, hcgtk_config,
  dependencies : [gtk4_dep, adw_dep, hccore_dep],
  include_directories : hcgtk_inc)

hcgtk_dep = declare_dependency(
  link_with : hcgtk_lib,
  dependencies : [gtk4_dep, adw_dep, hccore_dep],
  include_directories : hcgtk_inc)

executable('hourcalc-gtk',
  'main.cpp',
  dependencies : [hcgtk_dep],
  install : true,
  install_dir : get_option('libexecdir') / 'hourcalc')

if get_option('tests')
  # The real input group needs the X11 client library (its headers come with the GTK headers).
  x11_dep = dependency('x11', required : false)
  test_args = ['-DHC_TESTHOOKS']
  if x11_dep.found()
    test_args += ['-DHC_TEST_X11']
  endif
  hcgtk_test = executable('hourcalc-gtk-test',
    'main.cpp', 'testhooks.cpp', 'testutil.cpp', 'selftest.cpp', 'realinput.cpp', 'render.cpp',
    cpp_args : test_args,
    dependencies : [hcgtk_dep, x11_dep],
    install : false)

  # The headless self test needs a display of its own: it runs under xvfb-run (the program refuses to start
  # anywhere else, so no window can reach a desktop). Skipped when xvfb-run is not installed.
  xvfb_run = find_program('xvfb-run', required : false)
  if xvfb_run.found()
    test('gtk-selftest', find_program('sh'),
      args : [files('run-headless.sh'), hcgtk_test, '--selftest'],
      suite : 'gtk',
      timeout : 600)
  endif
endif
