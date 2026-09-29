#!/usr/bin/env bash
# Kiosk setup for the medical video system (Ubuntu 22.04, GNOME on Xorg).
#
# After this, powering on the device goes straight to the app:
#   1. GDM logs the desktop user in automatically (no Ubuntu login screen)
#   2. The app starts with the desktop session (autostart entry)
#   3. The screen never blanks or locks, so no password is asked later either
#   4. The few commands the app needs root for run through sudo without a password
#   5. The packages the app needs at runtime (video playback) are installed
#
# The app itself runs as the normal desktop user, not as root: a root GUI app breaks the
# user's X/audio session, and every recording it writes would become root-owned.
#
# Usage:
#   sudo deploy/kiosk-setup.sh                 # set up, for the user who ran sudo
#   sudo deploy/kiosk-setup.sh --user NAME --app /path/to/medical_qt_app
#   sudo deploy/kiosk-setup.sh --undo          # restore the normal login screen
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
KIOSK_USER="${SUDO_USER:-}"
APP_BIN="$REPO_DIR/build/medical_qt_app"
UNDO=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --user) KIOSK_USER="$2"; shift 2 ;;
        --app)  APP_BIN="$2"; shift 2 ;;
        --undo) UNDO=1; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

[[ $EUID -eq 0 ]] || { echo "Run with sudo." >&2; exit 1; }
[[ -n "$KIOSK_USER" && "$KIOSK_USER" != root ]] || { echo "Pass --user <desktop user>." >&2; exit 1; }
id "$KIOSK_USER" >/dev/null || exit 1
USER_HOME="$(getent passwd "$KIOSK_USER" | cut -d: -f6)"

GDM_CONF=/etc/gdm3/custom.conf
AUTOSTART="$USER_HOME/.config/autostart/medical_qt_app.desktop"
DCONF_PROFILE=/etc/dconf/profile/user
DCONF_KEYS=/etc/dconf/db/local.d/00-medical-kiosk
DCONF_LOCKS=/etc/dconf/db/local.d/locks/00-medical-kiosk
SUDOERS=/etc/sudoers.d/medical_qt_app
DCONF_DB=/etc/dconf/db/local
# What setup created that didn't exist before, so --undo can remove exactly that
CREATED=/etc/medical_qt_app-kiosk.created

record_created() {
    grep -qxF "$1" "$CREATED" 2>/dev/null || echo "$1" >> "$CREATED"
}

# Sets key=value in the [daemon] section of the GDM config, adding it if missing.
# Commented-out example lines are left alone.
set_gdm_key() {
    local key="$1" value="$2"
    if grep -qE "^\s*${key}\s*=" "$GDM_CONF"; then
        sed -i -E "s|^\s*${key}\s*=.*|${key}=${value}|" "$GDM_CONF"
    else
        sed -i -E "/^\[daemon\]/a ${key}=${value}" "$GDM_CONF"
    fi
}

if [[ $UNDO -eq 1 ]]; then
    # GDM: back to the file as it was before setup (automatic login off otherwise)
    if [[ -f "$GDM_CONF.before-kiosk" ]]; then
        cp -a "$GDM_CONF.before-kiosk" "$GDM_CONF" && rm -f "$GDM_CONF.before-kiosk"
    else
        set_gdm_key AutomaticLoginEnable False
    fi
    rm -f "$AUTOSTART" "$DCONF_KEYS" "$DCONF_LOCKS" "$SUDOERS"
    command -v dconf >/dev/null && dconf update
    # Then whatever setup had to create for them (only under /etc/dconf)
    if [[ -f "$CREATED" ]]; then
        while IFS= read -r entry; do
            case "$entry" in
                profile-line) sed -i '/^system-db:local$/d' "$DCONF_PROFILE" ;;
                /etc/dconf/*) rm -rf -- "$entry" ;;
            esac
        done < "$CREATED"
        rm -f "$CREATED"
    fi
    echo "Kiosk setup removed. The login screen returns after the next reboot."
    exit 0
fi

[[ -x "$APP_BIN" ]] || { echo "App binary not found: $APP_BIN (build it first or pass --app)" >&2; exit 1; }

# 1. Automatic login
[[ -f "$GDM_CONF.before-kiosk" ]] || cp -a "$GDM_CONF" "$GDM_CONF.before-kiosk"
set_gdm_key AutomaticLoginEnable True
set_gdm_key AutomaticLogin "$KIOSK_USER"
set_gdm_key WaylandEnable false   # the app's OpenGL/DeckLink preview is tested on Xorg
echo "✔ GDM automatic login for $KIOSK_USER"

# 2. Start the app with the session
install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "$(dirname "$AUTOSTART")"
sed "s|@APP_BIN@|$APP_BIN|" "$SCRIPT_DIR/medical_qt_app-autostart.desktop" > "$AUTOSTART"
chown "$KIOSK_USER:$KIOSK_USER" "$AUTOSTART"
echo "✔ Autostart entry $AUTOSTART"

# 3. Never blank, lock or suspend (system-wide dconf defaults, locked so they can't drift)
if [[ ! -f "$DCONF_PROFILE" ]]; then
    printf 'user-db:user\nsystem-db:local\n' > "$DCONF_PROFILE"
    record_created "$DCONF_PROFILE"
elif ! grep -q '^system-db:local' "$DCONF_PROFILE"; then
    echo 'system-db:local' >> "$DCONF_PROFILE"
    record_created profile-line
fi
[[ -d "$(dirname "$DCONF_KEYS")" ]] || record_created "$(dirname "$DCONF_KEYS")"
[[ -e "$DCONF_DB" ]] || record_created "$DCONF_DB"
install -d "$(dirname "$DCONF_KEYS")" "$(dirname "$DCONF_LOCKS")"
cat > "$DCONF_KEYS" <<'KEYS'
[org/gnome/desktop/session]
idle-delay=uint32 0

[org/gnome/desktop/screensaver]
lock-enabled=false
idle-activation-enabled=false
ubuntu-lock-on-suspend=false

[org/gnome/desktop/lockdown]
disable-lock-screen=true

[org/gnome/settings-daemon/plugins/power]
sleep-inactive-ac-type='nothing'
idle-dim=false
KEYS
cat > "$DCONF_LOCKS" <<'LOCKS'
/org/gnome/desktop/session/idle-delay
/org/gnome/desktop/screensaver/lock-enabled
/org/gnome/desktop/screensaver/idle-activation-enabled
/org/gnome/desktop/screensaver/ubuntu-lock-on-suspend
/org/gnome/desktop/lockdown/disable-lock-screen
/org/gnome/settings-daemon/plugins/power/sleep-inactive-ac-type
/org/gnome/settings-daemon/plugins/power/idle-dim
LOCKS
dconf update
echo "✔ Screen blanking, lock screen and auto-suspend disabled"

# 4. Password-less sudo for the commands the app needs (validated before installing)
TMP_SUDOERS="$(mktemp)"
sed "s|@KIOSK_USER@|$KIOSK_USER|g" "$SCRIPT_DIR/sudoers-medical_qt_app" > "$TMP_SUDOERS"
visudo -cf "$TMP_SUDOERS" >/dev/null
install -m 0440 -o root -g root "$TMP_SUDOERS" "$SUDOERS"
rm -f "$TMP_SUDOERS"
echo "✔ $SUDOERS"

# 5. Qt's GStreamer backend: without it recorded videos can't be played back in the app
RUNTIME_PKGS=(libqt5multimedia5-plugins)
MISSING_PKGS=()
for pkg in "${RUNTIME_PKGS[@]}"; do
    dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "install ok installed" || MISSING_PKGS+=("$pkg")
done
if [[ ${#MISSING_PKGS[@]} -gt 0 ]]; then
    apt-get update
    apt-get install -y "${MISSING_PKGS[@]}"
fi
echo "✔ Video playback packages installed"

echo
echo "Done. Reboot to check: the device should open the app with no password prompt."
echo "Undo with: sudo $0 --undo"
