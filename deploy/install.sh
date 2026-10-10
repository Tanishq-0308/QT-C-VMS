#!/bin/bash
# Sets up a Medical Video System machine from a fresh Ubuntu 22.04 (with internet).
#
#   sudo ./deploy/install.sh <deps folder> [options]
#   e.g. sudo ./deploy/install.sh ~/Downloads/ot-deps-2026-10
#
# <deps folder>: the ot-deps-YYYY-MM folder from the team drive (the same folder as for the OT
# Video System; this app uses its Blackmagic driver and FFmpeg). Everything else is downloaded.
#
# Options:
#   --dry-run          print every command, change nothing
#   --only <steps>     run only these steps (comma separated), e.g. --only app
#   --skip <steps>     run all steps except these
#
# Steps, in order: check base nvidia deps python app kiosk
# Every step can be run again: what is already installed is left as it is.
# Log: /var/log/medical-install.log
set -euo pipefail

# --- Arguments --------------------------------------------------------------------------------
DEPS=""
DRY_RUN=0
ONLY=""
SKIP=""
POSITIONAL=()
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1 ;;
        --only) ONLY="$2"; shift ;;
        --skip) SKIP="$2"; shift ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        *) POSITIONAL+=("$1") ;;
    esac
    shift
done
DEPS="${POSITIONAL[0]:-}"

REPO="$(cd "$(dirname "$0")/.." && pwd)"
USER_NAME="${SUDO_USER:-}"
CUDA_VERSION="12-9"
NVIDIA_DRIVER="580"
# The app finds sqlite.db, database/ and flask_zoom_api/ next to its build folder, so it is
# built in build/ inside the repository
APP_BIN="$REPO/build/medical_qt_app"

die() { echo "ERROR: $*" >&2; exit 1; }
if [ "$DRY_RUN" = 1 ] && [ "$(id -u)" -ne 0 ]; then
    USER_NAME="$(id -un)"   # a dry run only prints, so it may run without sudo
else
    [ "$(id -u)" -eq 0 ] || die "run with sudo: sudo $0 <deps folder>"
    [ -n "$USER_NAME" ] && [ "$USER_NAME" != root ] || die "run with sudo from the user who will use the app"
fi
USER_HOME="$(getent passwd "$USER_NAME" | cut -d: -f6)"

if [ "$DRY_RUN" = 0 ]; then
    exec > >(tee -a /var/log/medical-install.log) 2>&1
fi
echo "=== Medical install $(date '+%Y-%m-%d %H:%M') - repo $REPO ($(git -C "$REPO" describe --always --dirty 2>/dev/null || echo '?')), user $USER_NAME"

# --- Helpers ----------------------------------------------------------------------------------
run() {        # a command (printed; not executed in a dry run)
    echo "+ $*"
    [ "$DRY_RUN" = 1 ] || "$@"
}
as_user() {    # a command as the app's user
    run sudo -u "$USER_NAME" env HOME="$USER_HOME" "$@"
}
write_file() { # write_file <path> <content> (printed in a dry run)
    echo "+ write $1"
    if [ "$DRY_RUN" = 1 ]; then printf '%s\n' "$2" | sed 's/^/    | /'; else printf '%s\n' "$2" > "$1"; fi
}
want() {       # is this step selected?
    local step="$1"
    if [ -n "$ONLY" ]; then [[ ",$ONLY," == *",$step,"* ]]; return; fi
    [[ ",$SKIP," != *",$step,"* ]]
}
installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"; }
DONE=()
step_done() { DONE+=("$1"); echo "--- $1: done"; }

# --- check: Ubuntu version and the deps folder ------------------------------------------------
if want check; then
    echo "=== check"
    os_version="$(. /etc/os-release && echo "$VERSION_ID")"
    [ "$os_version" = "22.04" ] || die "Ubuntu 22.04 is required (this is $(. /etc/os-release && echo "$PRETTY_NAME"))"
    [ -n "$DEPS" ] || die "give the deps folder: sudo $0 <deps folder>"
    [ -f "$DEPS/SHA256SUMS" ] || die "$DEPS is not a deps folder (no SHA256SUMS)"
    (cd "$DEPS" && sha256sum --quiet -c SHA256SUMS) || die "the deps folder is incomplete or damaged (sha256sum -c SHA256SUMS)"
    step_done check
fi
DEPS="$(cd "${DEPS:-.}" && pwd)"

# --- base: Ubuntu packages --------------------------------------------------------------------
if want base; then
    echo "=== base packages"
    run apt-get update
    # Build tools; Qt 5 (+ GStreamer backend: plays recordings in the app); Poppler (report
    # viewer); Python for the report / zoom-camera service; exFAT tools for USB sticks
    run apt-get install -y build-essential cmake pkg-config git curl ca-certificates gnupg \
        software-properties-common dkms "linux-headers-$(uname -r)" linux-headers-generic \
        qtbase5-dev qtbase5-dev-tools qtmultimedia5-dev libqt5opengl5-dev libqt5sql5-sqlite \
        libgl1-mesa-dev libqt5multimedia5-plugins gstreamer1.0-plugins-good gstreamer1.0-libav \
        libpoppler-qt5-dev python3 python3-pip python3-reportlab exfatprogs
    step_done base
fi

# --- nvidia: driver and CUDA toolkit (compiler for the app's GPU code + runtime) --------------
if want nvidia; then
    echo "=== NVIDIA driver $NVIDIA_DRIVER and CUDA $CUDA_VERSION"
    if ! installed "nvidia-driver-$NVIDIA_DRIVER" && ! installed "nvidia-driver-$NVIDIA_DRIVER-open"; then
        grep -rqs graphics-drivers/ppa /etc/apt/sources.list.d/ || run add-apt-repository -y ppa:graphics-drivers/ppa
        run apt-get update
        run apt-get install -y "nvidia-driver-$NVIDIA_DRIVER"
    fi
    if ! installed "cuda-toolkit-$CUDA_VERSION"; then
        if ! installed cuda-keyring; then
            run curl -fsSL -o /tmp/cuda-keyring.deb \
                https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
            run dpkg -i /tmp/cuda-keyring.deb
            run apt-get update
        fi
        run apt-get install -y "cuda-toolkit-$CUDA_VERSION"
    fi
    step_done nvidia
fi

# --- deps: Blackmagic driver and FFmpeg (NVENC, LGPL) from the deps folder --------------------
if want deps; then
    echo "=== from the deps folder"
    installed desktopvideo || run apt-get install -y "$DEPS/blackmagic/desktopvideo_14.4.1a4_amd64.deb" \
        "$DEPS/blackmagic/desktopvideo-gui_14.4.1a4_amd64.deb"
    if [ ! -f /opt/ot-ffmpeg/lib/libavcodec.so ]; then
        run tar -C / -xzf "$DEPS"/libs/ot-ffmpeg-*-lgpl.tar.gz
    fi
    write_file /etc/ld.so.conf.d/ot-ffmpeg.conf "/opt/ot-ffmpeg/lib"
    run ldconfig
    # The report / zoom service runs "ffmpeg" (snapshots from the IP camera): this one, with NVENC
    for tool in ffmpeg ffprobe; do
        [ -e "/usr/local/bin/$tool" ] || run ln -s "/opt/ot-ffmpeg/bin/$tool" "/usr/local/bin/$tool"
    done
    step_done deps
fi

# --- python: the report / zoom-camera service (flask_zoom_api, started by the app) ------------
if want python; then
    echo "=== Python packages for flask_zoom_api"
    as_user python3 -m pip install --user -r "$REPO/flask_zoom_api/requirements.txt"
    as_user python3 -c "import flask, websockets, reportlab, onvif, requests; print('python packages ok')"
    step_done python
fi

# --- app: build the app from this repository --------------------------------------------------
if want app; then
    echo "=== the app"
    as_user env PKG_CONFIG_PATH=/opt/ot-ffmpeg/lib/pkgconfig \
        cmake -S "$REPO" -B "$REPO/build" -DCMAKE_BUILD_TYPE=Release \
        -DCUDA_TOOLKIT_ROOT_DIR="/usr/local/cuda-${CUDA_VERSION/-/.}"
    as_user cmake --build "$REPO/build" --target medical_qt_app -j"$(nproc)"
    step_done app
fi

# --- kiosk: start at boot, no screen lock or sleep, controlled updates ------------------------
if want kiosk; then
    echo "=== kiosk settings"
    # Automatic login on Xorg, the app starts with the session, no blanking / lock / sleep,
    # shortcuts that lead away from the app off (deploy/kiosk-setup.sh, --undo for maintenance)
    if [ "$DRY_RUN" = 1 ]; then
        echo "+ $REPO/deploy/kiosk-setup.sh --user $USER_NAME --app $APP_BIN"
    else
        "$REPO/deploy/kiosk-setup.sh" --user "$USER_NAME" --app "$APP_BIN"
    fi
    # Updates only when you install them: kernel, drivers and libraries held, no automatic or
    # pop-up updates (a new kernel makes the drivers rebuild; if that fails, no picture)
    if [ "$DRY_RUN" = 1 ]; then
        DRY_RUN=1 "$REPO/deploy/freeze-updates.sh"
    else
        "$REPO/deploy/freeze-updates.sh"
    fi
    step_done kiosk
fi

# --- Summary ----------------------------------------------------------------------------------
echo
echo "================================================================"
echo " Medical Video System install: ${DONE[*]:-nothing selected}"
[ "$DRY_RUN" = 1 ] && echo " (dry run: nothing was changed)"
echo
echo " Next:"
echo "  1. Reboot (NVIDIA / Blackmagic drivers, automatic login)."
echo "  2. The app starts by itself on the Dashboard."
echo "  3. Check: live video (DeckLink), recording, playback, snapshots, report (PDF),"
echo "     USB download, zoom camera - see deploy/DEPLOYMENT.md."
echo " Log: /var/log/medical-install.log"
echo "================================================================"
