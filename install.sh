#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

APT_PACKAGES=(
    build-essential
    meson
    ninja-build
    pkgconf
    libgtk-3-dev
    libxfce4panel-2.0-dev
    libwnck-3-dev
    xfce4-panel
)

REQUIRED_COMMANDS=(
    meson
    ninja
    pkg-config
    xfce4-panel
)

REQUIRED_PKGCONFIG=(
    gtk+-3.0
    libxfce4panel-2.0
    libwnck-3.0
)

missing=0

printf 'Checking build dependencies...\n'

for cmd in "${REQUIRED_COMMANDS[@]}"; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        printf '  missing command: %s\n' "$cmd" >&2
        missing=1
    fi
done

if command -v pkg-config >/dev/null 2>&1; then
    for module in "${REQUIRED_PKGCONFIG[@]}"; do
        if ! pkg-config --exists "$module"; then
            printf '  missing development library: %s\n' "$module" >&2
            missing=1
        fi
    done
fi

if (( missing )); then
    printf '\nThe following Debian/Ubuntu packages provide the required build tools and libraries:\n\n'
    printf '  %s\n' "${APT_PACKAGES[*]}"

    if command -v apt-get >/dev/null 2>&1; then
        printf '\nInstall them now? This will use sudo. [Y/n] '
        read -r answer
        case "${answer:-Y}" in
            [Yy]|[Yy][Ee][Ss])
                printf '\nRequesting administrator privileges...\n'
                sudo -v
                printf 'Updating APT package metadata...\n'
                sudo apt-get update
                printf 'Installing build dependencies...\n'
                sudo apt-get install -y "${APT_PACKAGES[@]}"
                ;;
            *)
                printf 'Dependency installation cancelled.\n' >&2
                exit 1
                ;;
        esac
    else
        printf '\nAutomatic dependency installation is only implemented for APT-based systems.\n' >&2
        printf 'Install the equivalent packages for your distribution and rerun ./install.sh.\n' >&2
        exit 1
    fi
fi

# Verify again after any package installation, so failures are explicit rather
# than surfacing later as opaque Meson errors.
for cmd in "${REQUIRED_COMMANDS[@]}"; do
    command -v "$cmd" >/dev/null 2>&1 || {
        printf 'ERROR: required command is still unavailable: %s\n' "$cmd" >&2
        exit 1
    }
done

for module in "${REQUIRED_PKGCONFIG[@]}"; do
    pkg-config --exists "$module" || {
        printf 'ERROR: required development library is still unavailable: %s\n' "$module" >&2
        exit 1
    }
done

# Use /usr, not /usr/local. The Xfce panel shipped by Ubuntu scans the normal
# system plugin locations, and /usr/local is not reliably included there.
if [[ -d build ]]; then
    meson setup --reconfigure build --prefix=/usr
else
    meson setup build --prefix=/usr
fi

meson compile -C build

printf '\nInstalling the plugin into /usr. This step uses sudo.\n'
sudo meson install -C build

# Refresh the panel only when an Xfce panel is actually running. This makes the
# installer safe to use before the first login to an Xfce session.
if pgrep -x xfce4-panel >/dev/null 2>&1; then
    printf 'Restarting the running Xfce panel...\n'
    xfce4-panel -r
else
    printf 'Xfce panel is not currently running; skipping panel restart.\n'
fi

cat <<'EOF2'

Installed Workspace Taskbar.
Open Panel Preferences -> Items -> + and add "Workspace Taskbar".

Sanity checks:
  ls /usr/share/xfce4/panel/plugins/workspace-taskbar.desktop
  find /usr/lib /usr/lib64 -path '*xfce4/panel/plugins*' -name '*workspace-taskbar*.so' 2>/dev/null
EOF2
