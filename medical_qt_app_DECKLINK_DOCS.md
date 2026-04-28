# Medical Qt App — DeckLink Version

A Qt/C++ desktop application for recording surgery videos. This version captures
HDMI/SDI signal from a **BlackMagic DeckLink** PCIe card, displays it live on
screen, and records to MP4 using GPU-accelerated H.264 encoding.

Source folder: `/home/brainwave/Desktop/medical_qt_app/`

---

## What the application does

1. **Live preview** of the operating-room camera on the dashboard
2. **Record** the surgery to an MP4 file, scoped to a specific patient/surgery
3. **Snapshots** to JPG during recording
4. **Add comments** linked to the recording (saved in DB)
5. **Zoom** an external PTZ camera (over network — separate from DeckLink)
6. **Reports** generated as PDFs (separate Flask sidecar)
7. **Patient / doctor / surgery management** through a SQLite database

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

---

## Folder layout

```
medical_qt_app/
+-- main.cpp                    starts QApplication, opens DB, launches Flask, shows MainWindow
+-- mainwindow.cpp/.hpp         stacks LoginPage / HomePage / RecordingPage
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
+-- core/                       AppController, ResponsiveWidget, UIScale
+-- database/                   DatabaseManager, SQL migration scripts
+-- widgets/                    misc shared widgets (VideoWidget, CameraZoomAPI, ...)
|
+-- ui/
|   +-- login/                  LoginPage
|   +-- home/                   HomePage  -- main shell, sidebar, page switching
|   +-- dashboard/              DashboardPage -- live preview + zoom controls
|   +-- patient/                PatientPage  -- patient list / CRUD
|   +-- SurgeryDetails/         per-patient surgery list
|   +-- SurgeryRecordPage/      per-surgery video/snapshot grid
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
+-- recordings/                 (output) MP4 files, organized by patient/surgery
+-- snapshots/                  (output) JPG snapshots
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
container. Owns the DeckLink discovery object. When the SDK reports a card
arrival, `addDevice()`:
- looks up `video_input` from settings (`"HDMI"` or `"SDI"`)
- configures the card via `IDeckLinkConfiguration`
- creates a single shared delegate that fans out frames to dashboard and
  recording pages
- starts capture in `bmdMode4K2160p30`

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

Requirements:
- BlackMagic Desktop Video driver (provides `libDeckLinkAPI`, `libDeckLinkPreviewAPI`)
- CUDA 12 + NVIDIA driver supporting NVENC
- FFmpeg dev libraries: `libavformat-dev libavcodec-dev libavutil-dev libswscale-dev`
- Qt5 dev: `qtbase5-dev qtmultimedia5-dev qtdeclarative5-dev`
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
- `recordings` (per-surgery MP4 paths), `snapshots` (per-surgery JPG paths)
- `comments_video` (timestamped comments tied to a recording)

---

## External dependencies running alongside the app

- **Flask sidecar** (`flask_zoom_api/app.py`) — started by `main.cpp` as a
  detached process on port 8001. Used for PTZ zoom over HTTP/WebSocket and
  ONVIF profile listing.
- **PDF generator** — separate endpoint on port 5000 that produces the
  surgery report from DB data.

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
