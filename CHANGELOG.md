# Changelog

## 0.1.3 — 2026-11-15
- Add an optional **Stretch to fill available panel width** setting (off by default).
- In stretch mode, redistribute fixed panel width among workspaces according to their natural icon widths, instead of making all workspaces equal.
- Use a constrained, non-scrolling-bar viewport for icon rows in stretch mode, so icon-count changes do not raise a workspace's minimum width and displace neighbouring panel widgets.
- Preserve existing settings, including minimum width and number of workspaces.

## 0.1.2 — 2026-11-15
- Size workspace compartments independently according to window icons.
- Make the per-workspace minimum width configurable (92 px by default).
- Stop requesting all remaining panel width from Xfce.
- Retain the Number of workspaces control introduced in 0.1.1.

## 0.1.1 — 2026-07-12
- Add a Number of workspaces (1–16) control to the Properties dialog.
- Change the desktop-wide workspace count via the window manager.

## 0.1.0 — 2026-07-05

First public release.
- Persistent equal-width workspace compartments.
- One icon per window, without application grouping.
- Active workspace, active window, and urgent-window highlighting.
- Click-to-switch, focus, restore, and minimize behavior.
- Standard libwnck window action menu on icon right-click.
- Per-plugin settings for workspace-number font size and icon spacing.
- Workspace-content swapping through a `Swap with` submenu.
- Pinned windows shown on every workspace and left untouched by swaps.
