# Deploying the Medical Video System

How to set up a new machine from a fresh Ubuntu 22.04. One script does the work:
`deploy/install.sh`. For updating a machine that is already set up, see `UPDATE_GUIDE.md`.

## 1. What you need

| | |
|---|---|
| Machine | NVIDIA GPU (RTX, compute 8.6), Blackmagic DeckLink card, internet |
| System | **Ubuntu 22.04** desktop, freshly installed; **Secure Boot off** in the BIOS (the NVIDIA and Blackmagic drivers are not signed) |
| User | the normal desktop user who will run the app (not root), with sudo rights |
| Code | this repository (GitHub) |
| Deps folder | `ot-deps-YYYY-MM` from the team drive: the **same folder as for the OT Video System**. This app uses its Blackmagic driver (`blackmagic/`) and FFmpeg (`libs/ot-ffmpeg-*`); the rest is for the OT app |

Everything else is downloaded by the installer.

## 2. Install

```bash
# 1. The code
git clone https://github.com/Tanishq-0308/QT-C-VMS.git ~/medical_qt_app

# 2. The deps folder: download it from the drive (e.g. to ~/Downloads) and check it
cd ~/Downloads/ot-deps-2026-10 && sha256sum -c SHA256SUMS      # every line: OK

# 3. Install (20-40 minutes)
cd ~/medical_qt_app
sudo ./deploy/install.sh ~/Downloads/ot-deps-2026-10

# 4. Restart
sudo reboot
```

After the reboot the machine logs in by itself and the app opens on the Dashboard.

`--dry-run` prints every command without changing anything. `--only <steps>` or
`--skip <steps>` choose steps; every step can be run again safely. Log:
`/var/log/medical-install.log`.

| Step | What it does |
|---|---|
| `check` | Ubuntu 22.04, the deps folder complete (checksums) |
| `base` | Build tools, Qt 5 (+ video playback plugins), Poppler (report viewer), Python 3, exFAT tools |
| `nvidia` | NVIDIA driver 580, CUDA toolkit 12.9 |
| `deps` | Blackmagic Desktop Video 14.4.1 and FFmpeg (NVENC, LGPL, `/opt/ot-ffmpeg`) from the deps folder; `ffmpeg` command for the report / zoom service |
| `python` | Python packages of the report / zoom-camera service (`flask_zoom_api/requirements.txt`, fixed versions) |
| `app` | Builds the app: `build/medical_qt_app` |
| `kiosk` | `kiosk-setup.sh` (below) and `freeze-updates.sh` (below) |

## 3. Kiosk mode (`deploy/kiosk-setup.sh`)

- Automatic login (on Xorg); the app starts with the session.
- The screen never blanks, locks or sleeps.
- Shortcuts that lead away from the app are off: Super key, Super+A/S/V, Super+1..9, Super+L,
  Ctrl+Alt+T, Ctrl+Alt+Del, hot corner, notification pop-ups. Alt+Tab still works.
- Password-less sudo only for `systemctl reboot/poweroff` (the app's Restart / Shut Down).

**Maintenance:** press **Ctrl+Alt+F3**, log in, then
`sudo ~/medical_qt_app/deploy/kiosk-setup.sh --undo && sudo reboot`: the normal login and
desktop are back. Afterwards turn kiosk mode on again with `sudo deploy/install.sh <deps> --only kiosk`
(or `sudo deploy/kiosk-setup.sh`).

## 4. Updates are frozen (`deploy/freeze-updates.sh`)

A new kernel makes the NVIDIA and Blackmagic drivers rebuild; if that fails there is no picture.
So the installer:

- holds the kernel, NVIDIA driver, CUDA, Blackmagic and GStreamer packages (`apt-mark hold`),
- turns off automatic updates, firmware updates and the Software Updater pop-up,
- holds snaps.

To update on purpose: `sudo deploy/freeze-updates.sh --undo`, then `sudo apt update && sudo apt upgrade`,
reboot, check live video and recording, and freeze again with `sudo deploy/freeze-updates.sh`.

## 5. Acceptance check

Work through `PRODUCTION_CHECKLIST.md` sections 6-8 with a **test patient**. In short:

- [ ] After the reboot: no login screen, the app opens on the Dashboard with live video.
- [ ] Record 30 s; it appears in the Archive and plays.
- [ ] Patient → surgery → recording, snapshots, comment; **Generate Report** opens the PDF.
- [ ] Download the report and a video to a USB stick (exFAT).
- [ ] Zoom camera (if connected) zooms.
- [ ] `curl -s http://127.0.0.1:8001/status` answers (report / zoom service).
- [ ] Restart and Shut Down from the app work without a password.
- [ ] Idle 30 minutes: the screen never blanks or locks.
- [ ] Super key and Ctrl+Alt+T do nothing; Ctrl+Alt+F3 shows the text login.

## 6. Where things are

| | |
|---|---|
| App | `~/medical_qt_app/build/medical_qt_app` |
| Database (patient data) | `~/medical_qt_app/sqlite.db`; created on the first start from `database/migrations/init.sql` |
| Recordings, snapshots | `~/medical_qt_app/recordings/`, `~/medical_qt_app/snapshots/` |
| Report / zoom service | `~/medical_qt_app/flask_zoom_api/` (started by the app, port 8001), log `flask.log` there |
| FFmpeg | `/opt/ot-ffmpeg` |
| Install log | `/var/log/medical-install.log` |

**Back up `sqlite.db`, `recordings/` and `snapshots/` before any update.** Never commit them.

## 7. If something goes wrong

| What you see | What to do |
|---|---|
| `the deps folder is incomplete or damaged` | Download the folder again; `sha256sum -c SHA256SUMS` must say OK for every file |
| Black screen / low resolution after the reboot | NVIDIA driver not loaded: Secure Boot must be off; `nvidia-smi` should list the GPU |
| No live video | `BlackmagicFirmwareUpdater status`; Blackmagic driver loaded after the reboot? (`lsmod \| grep blackmagic`) |
| Generate Report fails | `flask_zoom_api/flask.log`; `sudo deploy/install.sh <deps> --only python` |
| "Cannot play video" | `sudo deploy/install.sh <deps> --only base` (playback plugins) |
| Build fails | Send the end of `/var/log/medical-install.log`; rebuild with `sudo deploy/install.sh <deps> --only app` |

## 8. Licences

FFmpeg in the deps folder is built as **LGPL** (no GPL / non-free parts) and linked
dynamically from `/opt/ot-ffmpeg`. The Blackmagic Desktop Video driver is Blackmagic's,
installed from their package. The NVIDIA Video Codec SDK headers in `sdk/` are NVIDIA's.
