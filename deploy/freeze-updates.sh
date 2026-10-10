#!/bin/bash
# Keeps a medical video machine on its tested software: no kernel, driver or library updates unless
# you install them on purpose. A new kernel makes the DKMS drivers (NVIDIA, Blackmagic)
# rebuild; if one fails there is no picture until it is fixed.
#
#   sudo ./deploy/freeze-updates.sh           freeze (run by deploy/install.sh)
#   sudo ./deploy/freeze-updates.sh --undo    allow updates again, for planned maintenance:
#        then: sudo apt update && sudo apt upgrade, reboot, check live video and recording,
#        and freeze again
#   DRY_RUN=1 ./deploy/freeze-updates.sh       print what would be done
#
# What it does:
#  - apt-mark hold: kernel (also the meta packages that pull in new kernels), NVIDIA driver,
#    CUDA, Blackmagic, GStreamer - even a manual "apt upgrade" leaves them alone
#  - automatic updates off: unattended-upgrades, apt-daily timers, firmware updates (fwupd)
#  - no "Software Updater" pop-up for the user
#  - snaps held (no background snap updates)
set -euo pipefail

UNDO=0
[ "${1:-}" = "--undo" ] && UNDO=1
DRY_RUN="${DRY_RUN:-0}"
USER_NAME="${SUDO_USER:-$(id -un)}"
USER_HOME="$(getent passwd "$USER_NAME" | cut -d: -f6)"

if [ "$DRY_RUN" != 1 ] && [ "$(id -u)" -ne 0 ]; then
    echo "Run with sudo: sudo $0 ${1:-}" >&2
    exit 1
fi
run() {
    echo "+ $*"
    [ "$DRY_RUN" = 1 ] || "$@"
}

# The installed packages to hold: kernel, drivers and the libraries the cameras depend on
held_packages() {
    dpkg-query -W -f='${Package} ${Status}\n' \
        'linux-image-*' 'linux-headers-*' 'linux-modules-*' 'linux-generic*' 'linux-hwe-*' \
        'nvidia-*' 'libnvidia-*' 'cuda-*' 'libcudnn*' 'desktopvideo*' 'mediaexpress' \
        'v4l2loopback-dkms' 'gstreamer1.0-*' 'libgstreamer*' 2>/dev/null \
        | awk '$NF == "installed" {print $1}' | sort -u
}

TIMERS="apt-daily.timer apt-daily-upgrade.timer fwupd-refresh.timer"
NOTIFIER_SRC=/etc/xdg/autostart/update-notifier.desktop
NOTIFIER_USER="$USER_HOME/.config/autostart/update-notifier.desktop"

if [ "$UNDO" = 0 ]; then
    echo "=== Freezing updates"
    mapfile -t pkgs < <(held_packages)
    if [ "${#pkgs[@]}" -gt 0 ]; then
        echo "Holding ${#pkgs[@]} packages (kernel, NVIDIA, CUDA, Blackmagic, GStreamer, v4l2loopback)"
        run apt-mark hold "${pkgs[@]}"
    fi
    for t in $TIMERS; do
        systemctl list-unit-files "$t" >/dev/null 2>&1 && run systemctl disable --now "$t" || true
    done
    systemctl list-unit-files unattended-upgrades.service >/dev/null 2>&1 && \
        run systemctl disable --now unattended-upgrades.service || true
    echo "+ write /etc/apt/apt.conf.d/20auto-upgrades (all periodic updates off)"
    if [ "$DRY_RUN" != 1 ]; then
        cat > /etc/apt/apt.conf.d/20auto-upgrades <<'EOF'
APT::Periodic::Update-Package-Lists "0";
APT::Periodic::Download-Upgradeable-Packages "0";
APT::Periodic::AutocleanInterval "0";
APT::Periodic::Unattended-Upgrade "0";
EOF
    fi
    if [ -f "$NOTIFIER_SRC" ]; then
        echo "+ hide the Software Updater pop-up for $USER_NAME"
        if [ "$DRY_RUN" != 1 ]; then
            mkdir -p "$(dirname "$NOTIFIER_USER")"
            { cat "$NOTIFIER_SRC"; echo "Hidden=true"; } > "$NOTIFIER_USER"
            chown "$USER_NAME": "$NOTIFIER_USER"
        fi
    fi
    command -v snap >/dev/null && run snap refresh --hold || true
    echo "Updates frozen. To update on purpose: sudo $0 --undo"
else
    echo "=== Allowing updates (planned maintenance)"
    mapfile -t pkgs < <(apt-mark showhold)
    [ "${#pkgs[@]}" -gt 0 ] && run apt-mark unhold "${pkgs[@]}"
    for t in $TIMERS; do
        systemctl list-unit-files "$t" >/dev/null 2>&1 && run systemctl enable --now "$t" || true
    done
    command -v snap >/dev/null && run snap refresh --unhold || true
    run rm -f "$NOTIFIER_USER"
    echo "Updates allowed. Now: sudo apt update && sudo apt upgrade, reboot, check cameras and"
    echo "recording, then freeze again: sudo $0"
    echo "(Automatic updates stay off; they come back only if you re-enable them.)"
fi
