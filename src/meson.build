shared_module(
  'workspace-taskbar',
  'workspace-taskbar.c',
  dependencies: [glib, gtk, xfce_panel, wnck],
  c_args: ['-DWNCK_I_KNOW_THIS_IS_UNSTABLE'],
  gnu_symbol_visibility: 'hidden',
  install: true,
  install_dir: get_option('prefix') / get_option('libdir') / 'xfce4' / 'panel' / 'plugins',
)
