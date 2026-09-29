# Medical Qt App — DeckLink Version

A Qt/C++ desktop application for recording surgery videos. This version captures
HDMI/SDI signal from a **BlackMagic DeckLink** PCIe card, displays it live on
screen, and records to MP4 using GPU-accelerated H.264 encoding.

Source folder: `/home/brainwave/Desktop/medical_qt_app/`

---

## What the application does

1. **No login.** The device boots straight into the dashboard: Ubuntu logs in
   automatically and starts the app (see [Kiosk setup](#kiosk-setup-no-login-screen)),
   and the app shows no login page (the code is kept, unused, in `ui/login`)
2. **Live preview** of the operating-room camera on the dashboard
3. **Record** the surgery to an MP4 file, scoped to a specific patient/surgery
4. **Archive recordings.** The Dashboard's **⏺ Record** button starts recording at
   once, not tied to any patient, with no page change and no snapshots.
   - While it records, the button's red dot blinks and a blinking **● REC** timer
     shows in the top bar.
   - The user can go fullscreen or to any page while it records; recording continues
     until Record is pressed again.
   - Recordings are listed on the **Archive** page (sidebar)
5. **Snapshots** to JPG during recording
6. **Add comments** linked to the recording (saved in DB)
7. **USB export in the background.** Selected files are queued and copied to the
   pendrive one at a time, while the app stays fully usable. A **USB Transfers**
   drawer on the right shows each file's progress, speed and result
8. **Zoom** an external PTZ camera (over network — separate from DeckLink)
9. **Reports** generated as PDFs (separate Flask sidecar)
10. **Patient / doctor / surgery management** through a SQLite database

---

## Technology stack

| Layer | What it uses |
|---|---|
| GUI | **Qt 5** (Widgets, OpenGL, Sql, Network, Multimedia, Concurrent) |
| Capture card | **BlackMagic DeckLink SDK** (COM-style API, comes with `desktopvideo` driver) |
| Live preview | **OpenGL** via DeckLink's built-in `IDeckLinkGLScreenPreviewHelper` |
| Color conversion | **libswscale** (FFmpeg) — RGB32 → YUV420P on the CPU |
| Hardware encode | **NVENC** (NVIDIA H.264 encoder) via FFmpeg `h264_nvenc` |
| GPU framework | **CUDA 12** + NVIDIA Video Codec SDK 13 |
| Container/muxer | **FFmpeg** (libavformat) → `.mp4` files |
| Database | **SQLite** (Qt5 Sql) |
| PDF reports | **Poppler-Qt5** to view, **Flask** sidecar (`flask_zoom_api/`) to generate |
| PTZ camera control | HTTP CGI calls to the IP camera (DB-driven IP/credentials) |

---

## How the data flows

```
+------------------+    +-------------------+    +-------------------+
| DeckLink card    |--->| DeckLink SDK      |--->| Qt OpenGL widget  |
| (HDMI/SDI input) |    | callback delivers |    | (live preview)    |
+------------------+    | each video frame  |    +---------+---------+
                        +-------------------+              |
                                                           | grabFramebuffer
                                                           v (33 ms timer)
                                                 +-------------------+
                                                 | VideoRecorder     |
                                                 |  CPU sws_scale    |
                                                 |  RGB->YUV420P     |
                                                 |  upload to GPU    |
                                                 |  NVENC h264       |
                                                 |  -> .mp4 file     |
                                                 +-------------------+
```

1. The card sends each frame to the DeckLink driver
2. The driver fires a callback (`VideoInputFrameArrived`) with the frame
3. A Qt event delivers that frame to `DeckLinkOpenGLWidget` for display
4. While recording, a 33 ms timer grabs the OpenGL framebuffer (RGB32 image)
5. The image is converted to YUV420P on the CPU (libswscale)
6. It's uploaded to the GPU and fed into NVENC, which writes H.264 packets
7. FFmpeg's MP4 muxer assembles them into the final file

> **Side effect of the framebuffer-grab approach:** the recording resolution
> equals the *widget* size, not the *capture* resolution. If the dashboard
> renders at 1080p inside a smaller window, the file is 1080p even if the card
> is feeding 4K. The USB version fixes this — see the other doc.

### USB export

```
Gallery page                HomePage (lives as long as the app)
"Download Selected" ---->   TransferManager ---- queue: one job per file
                              |  one worker thread (QThreadPool, max 1)
                              |  copyFile(): 8 MiB reads, fdatasync every 64 MiB,
                              |  temp file + rename (QSaveFile)
                              v
                            progress every 200 ms
                              |
            +-----------------+------------------+
            v                                    v
  TransferDrawer (right-side panel)     Top-bar "USB Transfers" button
  one row per file: bar, MB/s,          shows "Copying N file(s)…",
  time left, Cancel                     turns red after a failure
```

1. The gallery (`SurgeryRecordingPage`) finds the stick with `UsbUtils::findUsbMount()`
   and calls `TransferManager::enqueue()`. It returns at once, and HomePage opens the drawer.
2. The manager copies one file at a time on its own thread. Two copies to one stick
   at the same time are slower than one after the other.
3. The gallery page can be closed or rebuilt mid-copy, because the manager belongs to
   HomePage, not to the page.
4. Before each file, the manager checks that:
   - the source file still exists;
   - the stick is still mounted;
   - the file fits in the free space;
   - the file is not 4 GB or larger when the stick is FAT32. FAT32 cannot hold such
     files, so the job fails with "format it as exFAT".
5. When the queue empties, a toast reports the result. Restart and Shut Down warn
   if a copy is still running.

---

## Folder layout

```
medical_qt_app/
+-- main.cpp                    starts QApplication, opens DB, launches Flask, shows MainWindow
+-- mainwindow.cpp/.hpp         holds HomePage (login page not used: the app opens on the dashboard; ui/login is kept for later)
+-- CMakeLists.txt              build system — links DeckLinkAPI, CUDA, NVENC, FFmpeg
|
+-- decklink/                   capture-layer code (DeckLink-specific)
|   +-- DeckLinkInputDevice     opens the card, configures input, receives frames
|   +-- DeckLinkDeviceDiscovery hot-plug detection (card arrival/removal events)
|   +-- DeckLinkOpenGLWidget    Qt OpenGL widget that draws frames + handles snapshots
|   +-- com_ptr.h               COM smart pointer helper
|
+-- include/                    BlackMagic DeckLink SDK headers (versioned)
+-- sdk/                        BlackMagic SDK extras
+-- cuda/                       NV12 -> RGBA CUDA kernel (used during rotation)
|
+-- core/                       AppController, ResponsiveWidget, UIScale,
|                               TransferManager (background USB copy queue),
|                               UsbUtils (find the mounted stick, FAT32 check)
+-- database/                   DatabaseManager, SQL migration scripts
+-- widgets/                    misc shared widgets (VideoWidget, CameraZoomAPI, ...),
|                               TransferDrawer (USB transfers panel), Toast
+-- deploy/                     kiosk-setup.sh (autologin + autostart + no lock screen),
|                               autostart .desktop entry, sudoers rule
|
+-- ui/
|   +-- home/                   HomePage  -- main shell, sidebar, page switching, USB transfers
|   +-- dashboard/              DashboardPage -- live preview, zoom, fullscreen, Record (general)
|   +-- patient/                PatientPage  -- patient list / CRUD
|   +-- SurgeryDetails/         per-patient surgery list
|   +-- SurgeryRecordPage/      video/snapshot grid: per surgery, or the Archive
|   +-- Recording/              RecordingPage + VideoRecorder (the encoder)
|   +-- Settings/               hospital info, video input (HDMI/SDI), etc.
|   +-- Profile/                user profile
|   +-- PdfViewerPage/          embedded PDF viewer
|   +-- AddCommentDialog/       AddPatientDialog/ AddSurgeryDialog/ EditPatientDialog/ EditSurgeryDialog/
|   +-- ClickableLabel/         small reusable widgets
|
+-- api/                        DB-backed controllers (one folder per entity)
|   +-- patients/  doctors/  surgeries/  videos/  images/  comments/
|   +-- camera_zoom/  company_setting/  system/  user/
|
+-- flask_zoom_api/             Python Flask sidecar (PTZ camera control via ESP32 / ONVIF)
+-- assets/                     icons, QSS stylesheets, logo, resources.qrc
+-- recordings/                 (output) MP4 files: <patient_id>/<surgery_id>/ or general/
+-- snapshots/                  (output) JPG snapshots: <patient_id>/<surgery_id>/ or general/
+-- sqlite.db                   the local database
```

---

## Key files in detail

### `decklink/DeckLinkOpenGLWidget`
The single most important widget. It:
- holds an OpenGL surface
- registers a callback (`DrawFrame`) the SDK calls for every new frame
- emits `frameArrived` so other widgets can subscribe
- in `paintGL()`: draws the current frame, applies rotation/flip, optionally
  draws a "Live - WxH" label, and (when recording) hands the framebuffer
  contents to the `VideoRecorder`

### `ui/Recording/VideoRecorder`
Owns the encoding pipeline. On `startRecording`:
- creates a CUDA stream
- builds an FFmpeg hardware-frames context (sw_format = YUV420P)
- opens `h264_nvenc` with preset=fast, rc=cbr
- spawns a thread that drains the frame queue, paces frames to 30 fps,
  uploads each to GPU and encodes
- on stop: flushes the encoder, writes the MP4 trailer, frees everything

### `ui/home/HomePage`
The application shell. Holds the sidebar, top bar, and the stacked page
container.
- **Sidebar, top:** Dashboard, Patients and Archive.
- **Sidebar, bottom:** Restart, Shut Down and Settings.
- **Top bar:** the **USB Transfers** button.

It also owns:
- the `TransferManager` and the `TransferDrawer`, so USB copies outlive whichever
  page started them;
- the long-lived Archive gallery (general media);
- the `RecordingSession` (below), which the capture device feeds, and the top-bar
  **● REC** indicator shown while an Archive recording runs (tap to go to the
  Dashboard to stop it). Restart and Shut Down warn while a recording is running.

It owns the DeckLink discovery object too. When the SDK reports a card
arrival, `addDevice()`:
- looks up `video_input` from settings (`"HDMI"` or `"SDI"`)
- configures the card via `IDeckLinkConfiguration`
- creates a single shared delegate that fans out frames to dashboard and
  recording pages
- starts capture in `bmdMode4K2160p30`

### `ui/Recording/RecordingSession`
The app's one recording at a time, owned by HomePage.
- It owns the `VideoRecorder`, which the capture device feeds directly.
- It builds the file path (`mediaDir()`) and inserts the `recordings` rows.
- It tracks the state: Idle, Starting, Recording or Saving.

Two users share it:
- **`DashboardPage`:** its Record button calls `start("", -1, flip)` and `stop()`.
  Archive files go to `recordings/general/`, and their rows have `patient_id` and
  `surgery_id` set to NULL. The page shows the blinking icon; the
  REC timer is in HomePage's top bar.
- **`RecordingPage`:** patient recordings. It only reacts to a recording it started
  itself (`m_ownsRecording`).

A second start while one is running is refused with a message; for example, starting a
patient recording while an Archive recording runs.

### `ui/SurgeryRecordPage/SurgeryRecordingPage`
The video and snapshot grid, with view, delete and download.
- **With a patient id:** shows one surgery's media, plus its details, Edit and
  Generate Report.
- **With an empty patient id:** it is the **Archive** page. It lists video rows
  `WHERE patient_id IS NULL`, newest first. It hides the surgery-only parts, Start
  Recording and the snapshot section.

Downloads are handed to the `TransferManager` (`setTransferManager()`) and go to
`<stick>/SurgeryDownloads/` or `<stick>/Archive/`.

### `core/TransferManager` and `widgets/TransferDrawer`
**`TransferManager`** is the background USB copy queue. It:
- tracks each job's state (Queued, Copying, Done, Failed or Cancelled), bytes
  copied, speed and error;
- lets you cancel a single job or all of them;
- emits a `queueDrained` summary when the queue empties.

The copy loop (`copyFile`):
- reads 8 MiB at a time;
- calls `fdatasync` every 64 MiB. Each sync is slow on a flash stick, and on FAT it
  also rewrites the allocation table, so fewer syncs is faster. The limit also caps
  how far the progress bar can run ahead of what is really on the stick;
- drops already-written pages from the page cache;
- writes to a temp file that is renamed over the destination only when complete.
  A pulled stick or a cancel never leaves a half-written file under the real name.

**`TransferDrawer`** is the slide-in panel that shows the queue. A tap outside it
closes it, and it never blocks the rest of the app.

### `ui/Settings/SettingsPage`
Hospital info form. The relevant capture-related field is **Video Input**: a
combo with hard-coded values `HDMI`, `SDI`, `AHD` stored in
`settings.video_input`. Saving the form re-emits `videoInputChanged`, which
HomePage listens for and uses to re-open the card on the new connector.

---

## Build and run

```bash
cd ~/Desktop/medical_qt_app
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./medical_qt_app
```

On the theatre device, the app is started automatically at boot. See
[Kiosk setup](#kiosk-setup-no-login-screen).

Requirements:
- BlackMagic Desktop Video driver (provides `libDeckLinkAPI`, `libDeckLinkPreviewAPI`)
- CUDA 12 + NVIDIA driver supporting NVENC
- FFmpeg dev libraries: `libavformat-dev libavcodec-dev libavutil-dev libswscale-dev`
- Qt5 dev: `qtbase5-dev qtmultimedia5-dev qtdeclarative5-dev`
- Video playback in the app: `libqt5multimedia5-plugins` (Qt's GStreamer backend) plus
  `gstreamer1.0-plugins-good gstreamer1.0-libav` (MP4 demuxer, H.264 decoder).
  Without the first, every video fails with "The QMediaPlayer object does not have a
  valid service"
- `libpoppler-qt5-dev`, `libopencv-dev`

The `CMakeLists.txt` hard-codes:
- DeckLink driver lib path: `/home/brainwave/Downloads/desktopvideo-14.4.1a4-x86_64/usr/lib/`
- NVENC SDK include path: `/home/brainwave/sdk/Video_Codec_SDK_13.0.19/Interface`

---

## Database

SQLite file at `sqlite.db`. Schema is created from
`database/migrations/init.sql` on each startup. Key tables:

- `settings` — single-row hospital configuration (logo, name, address, video_input, etc.)
- `patients`, `doctors`, `surgeries`
- `recordings` (per-surgery MP4 paths), `snapshots` (per-surgery JPG paths).
  **General** recordings and snapshots are rows with `patient_id` and `surgery_id`
  NULL, so no schema change was needed
- `comments_video` (timestamped comments tied to a recording)

---

## External dependencies running alongside the app

- **Flask sidecar** (`flask_zoom_api/app.py`) — started by `main.cpp` as a
  detached process on port 8001. Used for PTZ zoom over HTTP/WebSocket and
  ONVIF profile listing.
- **PDF generator** — separate endpoint on port 5000 that produces the
  surgery report from DB data.

---

## Kiosk setup (no login screen)

The login screen is Ubuntu's (GDM), not the app's, so running the app with `sudo`
does not remove it. The OS asks for the password before any app can start.
`deploy/kiosk-setup.sh` (run once with `sudo`) makes the device boot straight into the app:

| Step | What it changes |
|---|---|
| Automatic login | `/etc/gdm3/custom.conf`: `AutomaticLoginEnable=True`, `AutomaticLogin=<user>`, `WaylandEnable=false`. A backup is kept as `custom.conf.before-kiosk` |
| Start the app | `~/.config/autostart/medical_qt_app.desktop` runs `build/medical_qt_app` when the session starts |
| No lock or blank screen | locked dconf defaults in `/etc/dconf/db/local.d/00-medical-kiosk`: no idle blanking, no lock screen, no auto-suspend |
| Password-less sudo | `/etc/sudoers.d/medical_qt_app`, only for `systemctl reboot/poweroff` and `supervisorctl restart go-server` |

The app runs as the normal desktop user, not as root. A root GUI app breaks the X
and audio session, and it would write root-owned recordings. To go back to the
normal login, run `sudo deploy/kiosk-setup.sh --undo`.

---

## Why we replaced this version

The DeckLink approach works, but it has three real limitations:

1. **Hardware cost.** DeckLink cards are expensive. We had a 4-input ezcap
   USB HDMI capture card already.
2. **Recording resolution.** Frames go through the on-screen widget before
   reaching the encoder, so the recorded video matches the widget's pixel
   size, not the capture resolution.
3. **Card-locked code.** The DeckLink SDK is COM-style (`com_ptr`,
   `IUnknown`), needs a closed-source driver, and the proprietary preview
   helper is hard to extend.

The USB version (see `medical_qt_app_USB_DOCS.md`) addresses all three.
