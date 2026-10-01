# Production deployment checklist

For installing branch `ui-icons-headings` on the operating-theatre machine. Work through the
sections in order and tick each box. Anything that doesn't match the **Expected** result: stop,
note what you saw and collect the logs listed at the end.

Paths below assume the repo is at `~/medical_qt_app`; adjust if it is elsewhere.

## 1. Before you start

- [ ] **Back up the database and media.** `sqlite.db` holds the patient records.
  ```bash
  cd ~/medical_qt_app
  mkdir -p ~/backup_$(date +%F)
  cp -a sqlite.db ~/backup_$(date +%F)/
  cp -a recordings snapshots ~/backup_$(date +%F)/ 2>/dev/null || true
  ```
- [ ] Note the current commit so you can go back: `git -C ~/medical_qt_app log --oneline -1`
- [ ] Close the app if it is running.

## 2. Get the code

- [ ] ```bash
  cd ~/medical_qt_app
  git fetch
  git checkout ui-icons-headings
  git log --oneline -1        # Expected: e071546 or newer
  ```

## 3. Packages

- [ ] Video playback in the app:
  ```bash
  sudo apt install libqt5multimedia5-plugins gstreamer1.0-plugins-good gstreamer1.0-libav
  ```
- [ ] Report / zoom service:
  ```bash
  pip3 install --user -r ~/medical_qt_app/flask_zoom_api/requirements.txt
  python3 -c "import flask, websockets, reportlab, onvif, requests; print('ok')"
  ```
  **Expected:** `ok`

## 4. Database check

Archive recordings are stored without a patient. An old database may not allow that.

- [ ] ```bash
  cd ~/medical_qt_app
  python3 -c "import sqlite3; print(sqlite3.connect('sqlite.db').execute(\"select sql from sqlite_master where name='recordings'\").fetchone()[0])"
  ```
  **Expected:** the `patient_id` line has **no** `NOT NULL`. If it has `NOT NULL`, stop here and
  report it: Archive recordings would fail to save.

## 5. Build

- [ ] ```bash
  cd ~/medical_qt_app/build
  cmake .. && cmake --build . -j$(nproc)
  ```
  **Expected:** ends with `Built target medical_qt_app`, no errors.

## 6. First run by hand (before kiosk mode)

- [ ] Start it from a terminal: `cd ~/medical_qt_app/build && ./medical_qt_app`
- [ ] **Expected:** it opens on the **Dashboard** (no login screen) with live video and the
  overlay "Live • SDI/HDMI 1920x1080 @ …" in the top-left corner.
- [ ] In a second terminal: `curl -s http://127.0.0.1:8001/status`
  **Expected:** a short JSON answer within a second. The service log is
  `~/medical_qt_app/flask_zoom_api/flask.log` (or `~/.local/share/medical_qt_app/flask.log`).

## 7. Functional checks

Use a **test patient**, not a real one.

| # | Do this | Expected |
|---|---|---|
| 1 | Dashboard: tap zoom out, zoom in, rotate, fullscreen (then Esc) | Buttons show icons; the image rotates; fullscreen enters and leaves |
| 2 | Dashboard: tap the red Record button, wait 30 s, tap it again | The dot blinks and "● REC 00:00:xx" shows in the top bar; then "Recording saved to the Archive" |
| 3 | Sidebar → **Archive** | The recording from step 2 is listed first; tapping it plays it |
| 4 | Patients → test patient → surgery → **Start Recording** | The recording page opens **straight away**, without pressing any key |
| 5 | Record 30 s, take 2 snapshots, add a comment, stop, tap **Exit** | "Recording saved"; Exit returns to the patient's page **straight away** |
| 6 | Repeat 4–5 three times | The screen always changes without a key press. If it ever doesn't: note which monitor and whether a recording was running |
| 7 | Open the surgery again; tap a video | It plays. (If "Cannot play video" appears: section 3 packages are missing) |
| 8 | Tap **Generate Report** | The button says "Generating report…", then the PDF opens |
| 9 | Plug in a USB stick; select the video and snapshots; **Download Selected** | The Transfers panel opens with progress and MB/s; the app stays usable; a message says when it is done |
| 10 | During a copy, tap Restart | A warning says a copy is running; answer **No** |
| 11 | Zoom camera (ESP32), if connected: hold zoom in / out | The camera zooms (this part of the code is unchanged) |

## 8. Kiosk mode (boot straight into the app)

- [ ] ```bash
  cd ~/medical_qt_app
  sudo deploy/kiosk-setup.sh                 # for the user who runs sudo
  # or: sudo deploy/kiosk-setup.sh --user <desktop user> --app ~/medical_qt_app/build/medical_qt_app
  ```
  **Expected:** five lines starting with ✔ and "Done."
- [ ] Check the `go-server` restart the stream settings use:
  ```bash
  command -v supervisorctl && sudo -n supervisorctl status go-server
  ```
  **Expected:** a status line. If `supervisorctl` is missing, note it (only the stream-settings
  "restart" is affected).
- [ ] Reboot: `sudo reboot`
- [ ] **Expected:** no Ubuntu login screen; the app opens on the Dashboard by itself.
- [ ] Leave it idle for 30 minutes. **Expected:** the screen never blanks or locks.
- [ ] In the app: **Restart**, then later **Shut Down**. **Expected:** both work without asking for a
  password.

To undo kiosk mode: `sudo deploy/kiosk-setup.sh --undo`, then reboot.

## 9. If something fails

Send back:
- which step and what you saw (a phone photo of the screen helps)
- `~/medical_qt_app/flask_zoom_api/flask.log` (or `~/.local/share/medical_qt_app/flask.log`)
- the app's output: the terminal from step 6, or in kiosk mode
  `journalctl --user -b | grep -i medical`
- `git -C ~/medical_qt_app log --oneline -1`

To go back to the previous version: `git checkout <commit noted in section 1>`, rebuild (section 5),
and restore `sqlite.db` from the backup only if it was damaged.
