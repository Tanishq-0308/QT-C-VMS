# Updating the client machine

How to get a new version from the development machine onto the client's machine.
For the first installation and kiosk setup see `PRODUCTION_CHECKLIST.md`.

Paths assume the repo is at `~/medical_qt_app` and the branch is `ui-icons-headings`
(use `main` instead once the branch has been merged).

## 1. On the development machine: push

```bash
cd ~/medical_qt_app
git status                      # nothing uncommitted that should go along
git push origin ui-icons-headings
```

The last line should look like `abc1234..def5678  ui-icons-headings -> ui-icons-headings`.
GitHub asks for your username and token here.

## 2. On the client machine: update

Do this when no recording and no USB copy is running.

```bash
cd ~/medical_qt_app

# a. Back up the patient database
cp -a sqlite.db ~/sqlite.db.backup_$(date +%F_%H%M)

# b. Stop the app and the report/zoom service (the service keeps running after the app closes)
pkill -x medical_qt_app
pkill -f flask_zoom_api/app.py

# c. Bring back source files that were deleted after the last build (see section 4).
#    Restores only missing files; does nothing if none are missing.
git ls-files -d -z | xargs -0 -r git restore --

# d. Get the new code
git pull origin ui-icons-headings

# e. Build. Always run cmake: new source files are only picked up by it.
#    "cmake --build" uses whatever build tool the folder was set up with (make or ninja);
#    plain "make" can silently use stale build rules if the folder was set up for ninja.
cd build
cmake ..
cmake --build . -j$(nproc)

# f. Start
./medical_qt_app
```

In kiosk mode you can reboot instead of step f (`sudo reboot`): the app starts by itself.

## 3. Check after the update

- [ ] The app opens on the Dashboard with live video.
- [ ] `curl -s http://127.0.0.1:8001/status` answers within a second (report/zoom service).
- [ ] Open a test patient's surgery: recordings and snapshots are listed, with duration/size
      under each name; a video plays.
- [ ] Generate Report opens the PDF.
- [ ] Whatever the update was about works (see the commit messages: `git log --oneline -5`).

## 4. Removing the source code from the client machine (optional)

The built app does not need the source folders. After a successful build these can be deleted:

```
ui/  widgets/  core/  decklink/  api/  cuda/  include/  tests/  diag/  sdk/
main.cpp  mainwindow.cpp  mainwindow.hpp
```

**Never delete** (the running app needs them):

| Keep | Why |
|---|---|
| `build/medical_qt_app`, `build/logo.png` | the app and the hospital logo |
| `sqlite.db` | patient database |
| `database/migrations/init.sql` | read at every start; the app won't start without it |
| `flask_zoom_api/` | report and zoom service |
| `recordings/`, `snapshots/` | recorded media |
| `deploy/` | kiosk setup / undo, this guide |
| `.git/`, `CMakeLists.txt`, `assets/` | needed for the next update |

Before the next update, step 2c puts the deleted source files back. Without it the build fails
with an error like `fatal error: VideoRecorder.hpp: No such file or directory`.

## 5. If something goes wrong

| What you see | What to do |
|---|---|
| Build: `fatal error: <something>.hpp: No such file or directory` | Source files are missing: run step 2c, then `cmake .. && cmake --build . -j$(nproc)` again |
| Build: `undefined reference to …` for a file that exists | Build rules are stale: build with `cmake --build . -j$(nproc)`, not `make` |
| `git pull` refuses: "Your local changes would be overwritten" | A file was edited on this machine. Run `git status` and send the list before doing anything else |
| App doesn't appear after a reboot (kiosk mode) | `journalctl --user -b \| grep -i medical` and send the output |
| `error while loading shared libraries: libswresample…` | The binary is from an old build: rebuild (step 2e) |
| Generate Report fails | Read the message it shows, and send `flask_zoom_api/flask.log` (or `~/.local/share/medical_qt_app/flask.log`) |
| "Cannot play video" | `sudo apt install libqt5multimedia5-plugins`, restart the app |
| USB: "The stick is FAT32, which cannot hold files over 4 GB" | Format the stick as exFAT or NTFS (this erases it), then download again |
| The report service doesn't answer after the update | The old one may still be running: `pkill -f flask_zoom_api/app.py`, then restart the app |

**Going back to the previous version:**

```bash
cd ~/medical_qt_app
git log --oneline -5                 # find the commit that worked
git checkout <that commit>
cd build && cmake .. && cmake --build . -j$(nproc)
```

Restore `sqlite.db` from the backup only if the database itself was damaged; recordings made
since the backup would otherwise disappear from the lists.

## 6. Kiosk mode

| | |
|---|---|
| Turn on (auto login, app starts at boot, no lock screen) | `sudo deploy/kiosk-setup.sh` then `sudo reboot` |
| Turn off, back to how the system was | `sudo deploy/kiosk-setup.sh --undo` then `sudo reboot` |
| Close the app to work on the machine | `pkill -x medical_qt_app` from a terminal (Ctrl+Alt+T) |

Rebuilding the app does not change kiosk mode: it starts whatever is at `build/medical_qt_app`.
