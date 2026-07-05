#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

missing=0
for cmd in meson ninja pkg-config xfce4-panel; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        printf 'Missing command: %s
' "$cmd" >&2
        missing=1
    fi
done

if (( missing )); then
    cat >&2 <<'EOF'
Install dependencies on Ubuntu 24.04 with:
  sudo apt install build-essential meson ninja-build pkgconf libgtk-3-dev libxfce4panel-2.0-dev libwnck-3-dev
EOF
    exit 1
fi

# Use /usr, not /usr/local. The Xfce panel shipped by Ubuntu scans the normal
# system plugin locations, and /usr/local is not reliably included there.
if [[ -d build ]]; then
    meson setup --reconfigure build --prefix=/usr
else
    meson setup build --prefix=/usr
fi

meson compile -C build
sudo meson install -C build

# Refresh the panel so it re-reads the plugin descriptor list.
xfce4-panel -r

cat <<'EOF'

Installed Workspace Taskbar.
Open Panel Preferences -> Items -> + and add "Workspace Taskbar".

Sanity checks:
  ls /usr/share/xfce4/panel/plugins/workspace-taskbar.desktop
  find /usr/lib /usr/lib64 -path '*xfce4/panel/plugins*' -name '*workspace-taskbar*.so' 2>/dev/null
EOF
