# Updating the client machine

How to get a new version onto a machine set up with `deploy/install.sh`.
For the first installation see `DEPLOYMENT.md`.

The client machine has **no source code**: the installer removed it after building (step
`clean`). An update downloads the code from GitHub into a temporary folder, builds it and
removes it again. Patient data (`sqlite.db`, `recordings/`, `snapshots/`, reports) is not
touched.

## 1. On the development machine: push

```bash
cd ~/medical_qt_app
git status                      # nothing uncommitted that should go along
git push origin main && git push second-repo main
```

The client machine downloads `main` from `Brainwave-Technologie/medical_qt_app`.

## 2. On the client machine: update

Do this when no recording and no USB copy is running. Get to a terminal: Ctrl+Alt+F3 and log
in (kiosk mode), or Ctrl+Alt+T if kiosk mode is off.

```bash
# a. Back up the patient database
cp -a ~/medical_qt_app/sqlite.db ~/sqlite.db.backup_$(date +%F_%H%M)

# b. Stop the app and the report/zoom service (the service keeps running after the app closes)
pkill -x medical_qt_app
pkill -f flask_zoom_api/app.py

# c. Download, build, remove the code again (asks for the GitHub user name and token)
sudo ~/medical_qt_app/deploy/install.sh ~/Downloads/ot-deps-2026-10 --only app,clean

# d. The version now installed
cat ~/medical_qt_app/VERSION

# e. Start: reboot (kiosk mode starts the app)
sudo reboot
```

If the Python packages of the report service changed, add `python`: `--only python,app,clean`.

## 3. Check after the update

- [ ] The app opens on the Dashboard with live video.
- [ ] `curl -s http://127.0.0.1:8001/status` answers within a second (report/zoom service).
- [ ] Open a test patient's surgery: recordings and snapshots are listed, with duration/size
      under each name; a video plays.
- [ ] Generate Report opens the PDF.
- [ ] Whatever the update was about works.

## 4. If something goes wrong

| What you see | What to do |
|---|---|
| `git clone`: Authentication failed | Use a GitHub personal access token as the password; the account needs access to `Brainwave-Technologie/medical_qt_app` |
| Build fails | Send the end of `/var/log/medical-install.log` |
| `no code to remove (already removed?)` | Run `app` and `clean` together: `--only app,clean` |
| App doesn't appear after a reboot (kiosk mode) | `journalctl --user -b \| grep -i medical` and send the output |
| Generate Report fails | Read the message it shows, and send `flask_zoom_api/flask.log` (or `~/.local/share/medical_qt_app/flask.log`) |
| "Cannot play video" | `sudo ~/medical_qt_app/deploy/install.sh <deps folder> --only base`, restart the app |
| USB: "The stick is FAT32, which cannot hold files over 4 GB" | Format the stick as exFAT or NTFS (this erases it), then download again |
| The report service doesn't answer after the update | The old one may still be running: `pkill -f flask_zoom_api/app.py`, then restart the app |

**Going back to the previous version:** on the development machine, revert the faulty commit
(`git revert <commit>`) and push `main`; then update the client machine again (section 2).
Restore `sqlite.db` from the backup only if the database itself was damaged; recordings made
since the backup would otherwise disappear from the lists.

## 5. Kiosk mode

| | |
|---|---|
| Turn on (auto login, app starts at boot, no lock screen) | `sudo ~/medical_qt_app/deploy/kiosk-setup.sh` then `sudo reboot` |
| Turn off, back to how the system was | `sudo ~/medical_qt_app/deploy/kiosk-setup.sh --undo` then `sudo reboot` |
| Close the app to work on the machine | `pkill -x medical_qt_app` from a terminal (Ctrl+Alt+F3) |

Updating the app does not change kiosk mode: it starts whatever is at `build/medical_qt_app`.
