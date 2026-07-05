#!/usr/bin/env bash
set -euo pipefail

sudo rm -f /usr/share/xfce4/panel/plugins/workspace-taskbar.desktop
sudo find /usr/lib /usr/lib64 -path '*xfce4/panel/plugins*' -name '*workspace-taskbar*.so' -delete 2>/dev/null || true

if [[ -d build ]]; then
    sudo meson uninstall -C build || true
fi

xfce4-panel -r
printf 'Removed Workspace Taskbar plugin files.
'
