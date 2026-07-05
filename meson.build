project(
  'xfce4-workspace-taskbar-plugin',
  'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>=0.61.0',
  default_options: [
    'c_std=gnu11',
    'warning_level=3',
    'buildtype=debugoptimized',
  ],
)

cc = meson.get_compiler('c')

glib = dependency('glib-2.0', version: '>=2.66')
gtk = dependency('gtk+-3.0', version: '>=3.24')
xfce_panel = dependency('libxfce4panel-2.0', version: '>=4.16')
wnck = dependency('libwnck-3.0', version: '>=43.0')

add_project_arguments(
  cc.get_supported_arguments([
    '-Wall',
    '-Wextra',
    '-Wpedantic',
    '-Wshadow',
    '-Wformat=2',
    '-Werror=implicit-function-declaration',
    '-Wno-unused-parameter',
  ]),
  language: 'c',
)

subdir('src')
subdir('data')
