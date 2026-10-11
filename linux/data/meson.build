# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Everything around the two front ends (docs/LINUX.md, section 4): the launcher, the desktop
# file, the AppStream metainfo, the icons and the manual page.
#
# Included from the root meson.build, which defines app_id, app_version and source_url; the
# desktop file, the metainfo and the icon files are named after app_id, so changing the
# placeholder there is all it takes. The front ends install themselves into
# get_option('libexecdir') / 'hourcalc' (their own meson.build files).

# Absolute when libexecdir is (meson's / keeps the right-hand side then).
libexec_dir = get_option('prefix') / get_option('libexecdir') / 'hourcalc'

# AppStream wants an id for the developer too, in lower case: the application id without its
# last element (be.optimey.HourCalc -> be.optimey).
id_parts = app_id.split('.')
assert(id_parts.length() >= 3, 'app_id must be a reverse-DNS name with at least three elements')
developer_parts = []
foreach i : range(id_parts.length() - 1)
  developer_parts += id_parts[i]
endforeach
developer_id = '.'.join(developer_parts).to_lower()

data_conf = configuration_data()
data_conf.set('APP_ID', app_id)
data_conf.set('DEVELOPER_ID', developer_id)
data_conf.set('SOURCE_URL', source_url)
data_conf.set('VERSION', app_version)
data_conf.set('LIBEXECDIR', libexec_dir)

# /usr/bin/hourcalc: picks hourcalc-qt on KDE and hourcalc-gtk elsewhere
launcher = configure_file(
  input : 'hourcalc.in',
  output : 'hourcalc',
  configuration : data_conf,
  install : true,
  install_dir : get_option('bindir'),
  install_mode : 'rwxr-xr-x',
)

configure_file(
  input : 'hourcalc.desktop.in',
  output : app_id + '.desktop',
  configuration : data_conf,
  install : true,
  install_dir : get_option('datadir') / 'applications',
)

metainfo_template = files('hourcalc.metainfo.xml.in')[0]
if not fs.read(metainfo_template).contains('<release version="' + app_version + '"')
  warning('linux/data/hourcalc.metainfo.xml.in has no <release> for version ' + app_version)
endif
configure_file(
  input : metainfo_template,
  output : app_id + '.metainfo.xml',
  configuration : data_conf,
  install : true,
  install_dir : get_option('datadir') / 'metainfo',
)

configure_file(
  input : 'hourcalc.1.in',
  output : 'hourcalc.1',
  configuration : data_conf,
  install : true,
  install_dir : get_option('mandir') / 'man1',
)

# Icons: the scalable one is the Windows artwork itself, the PNGs are rendered from the 16 and
# 24 px drawings and from it by tools/make_linux_icons.py (committed under icons/). All are
# installed under the application id, which is the icon name of the desktop file.
icon_dir = get_option('datadir') / 'icons' / 'hicolor'
install_data(
  files('../../assets/icon/app.svg'),
  install_dir : icon_dir / 'scalable' / 'apps',
  rename : app_id + '.svg',
)
foreach size : ['16', '24', '32', '48', '64', '128', '256']
  install_data(
    'icons/hicolor/@0@x@0@/apps/hourcalc.png'.format(size),
    install_dir : icon_dir / '@0@x@0@'.format(size) / 'apps',
    rename : app_id + '.png',
  )
endforeach

# Needs nothing but sh and builds nothing, so it runs whatever the 'tests' option says
# (it is the one test the package build always runs).
test('launcher', find_program('sh'),
  args : [files('test-launcher.sh'), launcher],
  suite : 'data')
